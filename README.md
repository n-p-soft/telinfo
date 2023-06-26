TELINFO
=======

Ce programme permet de lire les données exportées par le compteur Linky. L'IDE
Arduino permet de le compiler et de le flasher sur une carte Arduino dotée d'un
ATMega328 et d'un afficheur LCD (shields Groove ou Seeduino).

Pour connecter la carte Arduino au compteur Linky, il faut fabriquer une petite
platine équipée essentiellement d'un optocoupleur. Il y a plusieurs montages
possible, faire une recherche sur internet pour les trouver. La longueur de la
liaison entre le compteur et la carte est théoriquement de plusieurs dizaines
de mètres au maximum.

Le compteur Linky peut fonctionner selon deux modes, "historique" (compatible
avec les anciens compteurs numériques) et "standard" (permet d'obtenir bien
plus d'informations, la liaison série est plus rapide). Pour savoir dans
quel mode votre compteur fonctionne, faites défiler les différents écrans.
Le passage du mode "historique" au mode "standard" se fait en contactant votre
fournisseur d'électricité (et non Enedis).

Versions du programme:
0.50: uniquement pour mode historique
0.60: version intermédiaire
0.70: pour mode standard
