# Arborescence MiniNux

```
/usr/sys/bin        commandes systeme, sans droits root
/usr/pentest/bin    outils de pentest, sans droits root
/root/sys/bin       commandes systeme reservees a root (admin)
/root/pentest/bin   outils de pentest necessitant root
/etc  /home  /tmp
```

`filesystem/rootfs/` est le modele de cette arborescence. Tout ce qui est sous
`/root` est reserve au compte administrateur. `ls [chemin]` parcourt l'arbre et
`which <commande>` donne le chemin d'une commande. Les repertoires pentest sont
vides pour l'instant.

Les fichiers de l'utilisateur (`write`, `cat`...) restent dans le stockage
persistant plat, hors de cette arborescence.
