# Mininux
Environnement Fedora lightweight & network security pour MacBook.

## Detection du materiel

Dans la console de MiniNux, lancer `hardware` pour enumerer les peripheriques PCI.
La sortie affiche leur adresse PCI, les identifiants vendeur/peripherique et
sous-systeme, la revision, la classe PCI (classe/sous-classe/interface) et des
pistes de pilote/firmware. Utiliser `hardware-next` et `hardware-prev` pour
parcourir les pages.

Les pistes ne sont pas une base exhaustive et ne garantissent pas la compatibilite.
La classe PCI et le fabricant seuls ne permettent pas de determiner les fichiers
firmware requis: verifier les identifiants complets et la documentation du pilote.
Cette commande detecte le materiel PCI mais n'identifie pas les peripheriques USB
individuels et n'installe ni ne charge encore de pilote ou firmware.

Pour un MacBook Air 13 pouces 2015, la commande `firmware` presente un profil
candidat uniquement si les identifiants PCI Broadcom BCM4360 (`14e4:43a0`) et
Intel HD 6000 (`8086:1626`) sont tous deux detectes. Elle distingue le pilote
Broadcom proprietaire, le microcode CPU et les pistes graphiques/reseau. Ce profil
n'est pas une liste complete: Bluetooth, camera et autres appareils USB doivent
etre enumeres avant de determiner leurs firmwares.

## Préparer une clé USB

Construire l’image UEFI dédiée :

```bash
make clean
make usb-image
```

Identifier la clé avant toute écriture :

```bash
lsblk -o NAME,SIZE,MODEL,TRAN,MOUNTPOINTS
```

Démonter uniquement ses partitions, puis écrire l’image sur le disque entier, par exemple :

```bash
sudo umount /dev/sdX1
sudo dd if=mininux-usb.img of=/dev/sdX bs=4M status=progress conv=fsync
```

Remplacer `/dev/sdX` par le périphérique réel de la clé, jamais par une partition et jamais par le disque système.

L'image UEFI contient une partition EFI FAT32 de type MBR et le chemin
amovible standard `EFI/BOOT/BOOTX64.EFI`. Ce programme inventorie les
périphériques exposés par le firmware via `EFI_USB_IO_PROTOCOL`, affiche leurs
VID:PID/classe/révision et sauvegarde le résultat dans `MININUX.TXT` à la racine
de la partition. Ce diagnostic UEFI n'exécute pas encore le noyau BIOS 32 bits.

Après le démarrage, récupérer le rapport sur Fedora :

```bash
make read-report DEV=/dev/sdX
```

La cible lit `/dev/sdX1` et crée `rapport-mininux.txt`.

## Inventaire USB (xHCI)

La commande BIOS `usb` prend le controleur xHCI en charge, enumere les peripheriques
(hubs USB2 compris, profondeur 2) et affiche VID:PID, classe, vitesse et une
indication de firmware. `usb-next` et `usb-prev` changent de page.

Attention : sur un vrai Mac, la prise de controle de l'xHCI peut couper
l'emulation clavier PS/2 du BIOS. Le clavier peut cesser de repondre apres
`usb` ; il faut alors redemarrer.

## Rapport automatique sur la cle

La commande BIOS `report` lance `hardware`, `firmware` puis `usb` toute seule et ecrit
le texte dans les secteurs 64 a 191 de la cle de demarrage (via INT 13h, en
repassant brievement en mode reel). Cette zone est hors de l'image (50 secteurs) :
elle survit aux redemarrages et a un nouveau `dd` de l'image.

Le rapport est ecrit deux fois : apres la phase PCI/firmware, puis apres le scan
USB (si le BIOS ne sait plus ecrire sur la cle apres la prise de controle de
l'xHCI, la phase PCI reste lisible). Sur Fedora :

    make read-bios-report DEV=/dev/sdX    # cree rapport-mininux.txt (sudo si besoin)

## Comptes, fichiers et persistance (noyau BIOS)

- Premier démarrage : création du compte admin (mot de passe 8–40 caractères).
- Mots de passe : PBKDF2-HMAC-SHA256 salé ; temporisation après échecs.
- Commandes : `help whoami users logout passwd ls files cat write [-s] append rm`;
  admin : `useradd <nom> [admin]`, `userdel`, `usb`, `report`.
- Persistance : deux copies A/B (LBA 256 et 272) sur le disque de boot, validées par SHA-256.
- Limites : pas de chiffrement ; un accès disque hors ligne permet de réécrire le stockage.
  Entropie faible sans RDRAND. Testé uniquement sous QEMU (BIOS), pas sur le Mac.

## Arborescence

```
src/boot/      secteur de démarrage BIOS (boot.asm)
src/kernel/    noyau : kernel.c, usb, crypto, persist (.c/.h)
src/tinyc/     mini compilateur C
src/uefi/      application UEFI (boot.c, font8x16.h)
filesystem/    manifeste du système de fichiers
scripts/       scripts de dev (dev.sh)
build/         objets et binaires intermédiaires (ignoré par git)
```

`make` produit `mininux.img` (BIOS) ; `make mininux-uefi.img` produit l'image UEFI.

## Racine du système

`/usr/sys/bin` (commandes sans root), `/root/sys/bin` (commandes admin),
`/usr/pentest/bin` et `/root/pentest/bin` (outils de pentest, vides pour l'instant),
plus `/etc /home /tmp`. Commandes `ls [chemin]` et `which <cmd>`. Voir `filesystem/README.md`.

## Pilotes chargés au démarrage

`src/kernel/driver.c` contient une table de pilotes. Au boot, le noyau parcourt le bus PCI,
associe chaque périphérique à un pilote (vendor/device ou classe) et appelle son `init`.
La commande `drivers` affiche l'état : `charge`, `SANS PILOTE` ou `ECHEC`.
Chargés aujourd'hui : vga-text, ps2-kbd, bios-disk, xhci (activation mémoire + bus mastering).
Reconnus mais sans pilote : ahci, nvme, bcm4360 (Wi-Fi interne), Intel graphics, audio.

## Terminal UEFI (pour le Mac)

L'app UEFI (`mininux-uefi.img`) ouvre après le scan USB le même terminal que le noyau BIOS :
premier démarrage (création de l'admin), login, utilisateurs, fichiers, arborescence `/usr/sys/bin`
et `/root/sys/bin`. Les données sont dans `MNDISK.IMG` sur la partition EFI de la clé
(persistant, lisible depuis Fedora). Le clavier passe par le firmware (ConIn).
Le scan USB reste dans `MININUX.TXT` et s'affiche avec la commande `usb`.
