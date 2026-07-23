# Linux Userspace Drajver za senzor TMD37253
## Uvod
Ovaj projekat implementira uređaj za detekciju blizine, mjerenje osvijetljenosti i hromatskog spektra okruženja.  
Korišten je senzor blizine, boje (RGBC+IR) i ambijentalnog osvjetljenja *TMD37253* integrisan na razvojnu pločicu *Light mix-sens Click*.  
Linux *Userspace* drajver koji je razvijen da ispuni zahtjeve projekta je testiran na *DE1-SoC* razvojnoj ploči na kojoj je pokrenut embedded Linux sistem izgrađen pomoću *Buildroot* alata.

## Buildroot
Sistem koji smo izgradili u toku kursa Ugrađeni Računarski Sistemi je potrebno minimalno modifikovati za omogućavanje testiranja ovog projekta.  
Ukratko ono što smo imali do sada:

- *Buildroot 2024.02*
- Linux kernel *socfpga-6.1.38-lts*
- Bootloader *U-Boot 2024.01*
  
Defaultna konfiguracija u okviru *Buildroot* za našu *DE1-SoC* razvojnu ploču se nalazi u *buildroot/configs/terasic_de1soc_cyclone5_defconfig*.  
Defaultna konfiguracija za Linux kernel našeg sistema se nalazi u *buildroot/board/terasic/de1soc_cyclone5/de1_soc_defconfig*.  
Defaultna konfiguracija za *U-Boot* bootloader *socfpga_de1_soc* se učitava iz izvornog stabla *U-Boot* *(In-tree board defconfig file)*.  

Napravljen je *rootfs overlay* u koji je smješten fajl *board/terasic/de1soc_cyclone5/rootfs-overlay/etc/systemd/network/70-static.network* koji omogućava da *systemd* infrastruktura ispravno konfiguriše mrežni interfejs.  
Dodat je *Dropbear* sofverski paket za omogućenje pristupa ploči preko *SSH* protokola, te zatim u *rootfs overlay* smješten javni ključ generisan pomoću *ssh-keygen* alata u fajl *board/terasic/de1soc_cyclone5/rootfs-overlay/root/.ssh/authorized_keys*.  
### Toolchain
Korišćen je alat *Crosstool-NG* za generisanje toolchain-a, odabrana je početna konfiguracija *arm-cortexa9_neon-linux-gnueabihf* a zatim odabrane sledeće opcije:
```
Languages       : C,C++
OS              : linux-6.1.35
Binutils        : binutils-2.40
Compiler        : gcc-13.2.0
C library       : glibc-2.38
Debug tools     : gdb-13.2
Companion libs  : expat-2.5.0 gettext-0.21 gmp-6.2.1 isl-0.26 libiconv-1.16 mpc-1.2.1 mpfr-4.2.1 ncurses-6.4 zlib-1.2.13 zstd-1.5.5
Companion tools :
```
Ovaj toolchain je dodat kao eksterni u Buildroot sistem sa apsolutnom putanjom prema istom(za reprodukciju build-a potrebno prilagoditi).
 
### Modifikacije
Da bismo mogli da koristimo I2C periferiju, koja nam je potrebna za komunikaciju sa senzorom, potrebno je da eksportujemo signale HPS periferija (među kojima je i I2C2) preko *FPGA Interconnect* prema FPGA dijelu *Cyclone V SoC* a zatim kroz FPGA Fabric povežemo ove signale na pinove GPIO konektora.    

Ovo se postiže tako što se prilagodi konfiguracija SPL-a i U-Boot primjenom patch-a *(de1-soc-handoff.patch)* koji se na repozitorijumu nalazi u 
*buildroot/board/terasic/de1soc_cyclone5/patches/uboot/* direktorijumu. Ovo se u Buildroot-u automatizuje na sledeći način:
```
make menuconfig
```
A zatim u meniju za modifikovanje Buildroot konfiguracije potrebno je postaviti: 
```text
Bootloaders
└── [*] U-Boot
    └── (board/terasic/de1soc_cyclone5/patches/uboot) Custom U-Boot patches
```
Fajl *buildroot/board/terasic/de1soc_cyclone5/boot-env.txt* za *U-Boot* okruženje je prilagođen da bi se uzele u obzir promjene.  
*U-Boot* za programiranje FPGA prema izmjenama treba da ima pristup konfiguracionom fajlu *socfpga.rbf*, ovaj fajl je potrebno 
kopirati na FAT32 particiju na SD kartici prije pokretanja sistema. 
Da bismo izbjegli ručno kopiranje ovo se takođe može automatizovati u okviru *Buildroot* na sledeći način:
```text
System configuration
└── (board/terasic/de1soc_cyclone5/post-image.sh support/scripts/genimage.sh) Custom scripts to run after creating filesystem images
```
Ovime dodajemo skriptu *post-image.sh* koja treba da se nalazi prije *genimage.sh* da bi to bio redoslijed izvršavanja.
Ovde *post-image.sh* vrši kopiranje *socfpga.rbf* u *output/images* , a zatim ga skripta *genimage.sh* kopira direktno u *sdcard.img* na FAT32 particiju.
Za ovu funkcionalnost je još samo potrebno modifikovati *genimage.cfg*:
```
image boot.vfat {
	vfat {
		files = {
			"zImage",
			"socfpga_cyclone5_de1_soc.dtb",
			"socfpga.rbf"
		}
	}
 size = 16M
}
```
tako da dodamo naš konfiguracioni fajl i povećamo veličinu *boot.vfat*.


Naš Userspace drajver je implementiran kao paket u okviru eksternog *Buildroot* stabla, ovo olakšava nadogradnju *Buildroot*-a na novu verziju kao i prenos projekta na drugi sistem, jer nije potrebno dodavanje paketa u samo *Buildroot* stablo.  
Da bismo ga dodali u postojeću infrastrukturu potrebno je prvo uopšte registrovati ovo eksterno stablo:
```
make BR2_EXTERNAL=path-to/tmd3725-br-external menuconfig  #if no previously existing external tree
```
A zatim uključiti: 
```text
External options
└── External Buildroot tree for the TMD3725 color sensor (RGBC+IR),ambient light and proximity sensor
    └── [*] tmd3725-userspace-driver
```
i pokrenuti *make* za build, ovo će nam generisati aplikaciju i konfiguracioni fajl */usr/bin/tmd3725_sensor* i */etc/tmd3725.conf*  

Ukoliko se prave modifikacije našeg userspace drajvera onda radimo:
```
make tmd3725-userspace-driver-rebuild
make
```
Ukoliko želimo da ga uklonimo radimo:
```
make BR2_EXTERNAL="" menuconfig
make clean
```
ili
```
make BR2_EXTERNAL="" menuconfig
make tmd3725-userspace-driver-dirclean # removes output/build/tmd3725-userspace-driver/
rm output/target/usr/bin/tmd3725_sensor
rm output/target/etc/tmd3725.conf
```
gdje moramo da ručno uklonimo fajlove iz *output/target* jer Buildroot radi inkrementalan build i neće ih ukloniti pri sledećem *make*.  
Više o funkcionalnosti eksternog stabla u *Buildroot*-u: [External tree Buildroot](https://buildroot.org/downloads/manual/manual.html#outside-br-custom)
