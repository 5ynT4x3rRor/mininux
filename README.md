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

Construire l’image dédiée :

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

## Inventaire USB (xHCI)

La commande `usb` prend le controleur xHCI en charge, enumere les peripheriques
(hubs USB2 compris, profondeur 2) et affiche VID:PID, classe, vitesse et une
indication de firmware. `usb-next` et `usb-prev` changent de page.

Attention : sur un vrai Mac, la prise de controle de l'xHCI peut couper
l'emulation clavier PS/2 du BIOS. Le clavier peut cesser de repondre apres
`usb` ; il faut alors redemarrer.
