# RFC — Protocole réseau KUGE

*2026-10-20 · protocole version 2*

KUGE parle un protocole applicatif unique, en couches, sur UDP ou TCP : une poignée de main, deux canaux (non fiable, fiable) et des messages typés dont l'identifiant est le hash du nom.

## Statut de ce mémo

Ce document est **informatif** : il décrit le protocole version 2 (`EndpointConfig::protocol = 2` par défaut ; la version 1 est celle de KUGE v0.1.0) tel que le code l'implémente dans `modules/net` et `modules/replication`. En cas de divergence, le code fait foi. Les mots MUST, SHOULD et MAY ont le sens de la RFC 2119.

## Résumé

Le protocole empile quatre couches.

1. **Paquet** : un paquet entier de 8192 octets au plus, porté par un datagramme UDP ou par un flux TCP découpé en trames.
2. **Session** : une poignée de main `Connect` / `Challenge` / `Accept` / `Reject` qui prouve l'adresse du client, un `Goodbye`, un keep-alive, un délai de silence, et une étiquette de 4 octets sur chaque paquet qui suit.
3. **Canal** : `Unreliable` (envoyé une fois) ou `Reliable` (numéroté, acquitté, renvoyé, livré dans l'ordre).
4. **Message** : un identifiant de 4 octets (FNV-1a du nom du type) suivi des champs, en little-endian.

Au-dessus de ces quatre couches, KUGE définit des échanges applicatifs : le lobby et les salles, puis la réplication de l'état du monde par instantanés différentiels.

## 1. Introduction

KUGE est un moteur de jeu en C++ ; son protocole réseau permet à un client de trouver une salle, de s'y connecter et de voir le même monde que les autres joueurs. Le même code sert pour un serveur dédié, pour un joueur qui héberge sa partie (transport en mémoire) et pour les tests.

Objectifs :

- **Un seul modèle de message** pour le lobby, la salle et le jeu : un type C++ déclaré par `KUGE_MESSAGE`, sans fichier de description séparé.
- **Même comportement sur UDP et sur TCP** : la poignée de main et les canaux sont les mêmes, seul le transport change.
- **Une lecture qui ne fait pas confiance** : une donnée mal formée est abandonnée et comptée, jamais lue hors limites.
- **Un état du monde reconstruit par différences**, pour qu'un paquet perdu coûte seulement un paquet suivant plus gros.

Hors périmètre : le contenu des composants d'un jeu donné (chaque jeu enregistre les siens), le chiffrement et l'authentification des joueurs (voir la section 10), la découverte de serveurs sur Internet.

## 2. Terminologie et conventions

| Terme | Sens |
| --- | --- |
| Paquet | Une unité que le transport livre entière, de 1 à 8192 octets (`MAX_PACKET`). |
| Endpoint | Un bout du réseau : un serveur qui accepte des connexions, ou un client qui en fait une. |
| Connexion | La relation entre deux endpoints ; chaque sens a son propre canal fiable. |
| Canal | `Unreliable` (0) ou `Reliable` (1). |
| Message | Un identifiant de 4 octets suivi des champs d'un type `KUGE_MESSAGE`. |
| Lobby | Le serveur à l'adresse connue, qui place les joueurs dans des salles. |
| Salle (room) | Une partie, avec sa propre adresse, son propre port et son monde. |
| NetworkId | Le numéro d'une entité répliquée dans une salle, jamais réutilisé. |
| Tick | Un pas de la simulation fixe du serveur (60 par seconde par défaut). |

**Représentation des valeurs.** Tout est en little-endian, avec la taille exacte du type.

| Type | Sur le fil |
| --- | --- |
| entiers | 1, 2, 4 ou 8 octets, sans signe ou complément à deux |
| `bool` | 1 octet : 0 ou 1. Toute autre valeur est une erreur. |
| `float`, `double` | IEEE 754, 4 ou 8 octets |
| enum | son type sous-jacent |
| chaîne | longueur sur 4 octets, puis les octets (UTF-8 par convention) |
| vecteur | nombre d'éléments sur 4 octets, puis les éléments |
| tableau fixe | les éléments, sans compteur |
| `optional` | un `bool`, puis la valeur s'il vaut 1 |
| `Vec2` | `x`, `y` : 2 `float` |
| `Rect` | `x`, `y`, `w`, `h` : 4 `float` |
| message imbriqué | ses champs, sans identifiant |

Un lecteur MUST refuser un vecteur dont le nombre d'éléments dépasse les octets restants, une chaîne plus longue que les données, un `bool` hors de {0, 1} et un message qui laisse des octets après son dernier champ.

## 3. Architecture et transport

Un client parle à deux endpoints l'un après l'autre : le lobby (adresse connue, TCP par défaut), puis une salle (UDP, un port par salle). Les deux emploient les mêmes couches.

```
+--------------------------------------------------------------+
| Application  Lobby, salle (Hello...)  |  Réplication, entrées |
+--------------------------------------------------------------+
| Message      id u32 (FNV-1a du nom) + champs                 |
+--------------------------------------------------------------+
| Canal        Unreliable | Reliable (seq, ack, renvoi)        |
+--------------------------------------------------------------+
| Session      Connect / Challenge / Accept / Reject / Goodbye |
+--------------------------------------------------------------+
| Paquet       UDP : un datagramme | TCP : trame u32 + paquet  |
+--------------------------------------------------------------+
```

| Transport | Usage par défaut | Ce que le transport garantit |
| --- | --- | --- |
| UDP | salles, ports 4300 à 4363 | un datagramme = un paquet ; perte, doublon et désordre possibles |
| TCP | lobby, port 4242 | fiable et ordonné ; chaque paquet est précédé de sa longueur |
| Loopback | client et serveur du même processus | en mémoire, même interface ; ne promet rien |

Un transport MUST livrer des paquets entiers de 1 à 8192 octets (`MAX_PACKET`). Sur TCP, un paquet est précédé de sa longueur sur 4 octets (u32 little-endian) ; un récepteur MUST fermer la connexion si cette longueur vaut 0 ou dépasse 8192.

Un client MUST joindre une salle en UDP, à l'adresse et au port donnés par `RoomAssigned` ; une adresse vide désigne l'hôte utilisé pour le lobby. Les identifiants de connexion (`ConnectionId`) sont locaux à un endpoint et ne circulent jamais sur le fil. Sur UDP, un pair est reconnu à son adresse source.

## 4. Format des paquets et des messages

Le premier octet d'un paquet donne son type ; les champs suivent dans l'ordre, en little-endian.

| Type | Octet 0 | Champs suivants | Taille (octets) |
| --- | --- | --- | --- |
| Connect | 1 | magic `KUGE` (`4B 55 47 45`), version du protocole (u16), sel du client (u64), cookie (u64, 0 au premier) | 23 |
| Accept | 2 | le sel du client (u64), le cookie (u64) | 17 |
| Reject | 3 | 0 (u32) | 5 |
| Data | 4 | canal (u8), ack (u32), ackBits (u32), seq (u32, canal fiable seulement), message, étiquette (u32) | 14, ou 18 + message |
| Goodbye | 5 | étiquette (u32) | 5 |
| Challenge | 6 | le sel du client (u64), le cookie (u64) | 17 |

Le champ canal d'un paquet `Data` vaut 0 (`Unreliable`), 1 (`Reliable`) ou 2 (contrôle). Un paquet de contrôle n'a ni seq ni message : c'est un simple accusé ou un keep-alive. Toute valeur de canal supérieure à 2 est invalide. **L'étiquette** termine chaque paquet `Data` et `Goodbye` : les 32 bits de poids faible de SipHash-2-4 de tout ce qui la précède, avec la clé (`sel` XOR `0x5352565253525652` si le paquet vient du serveur, `cookie`) ; un paquet dont l'étiquette est fausse est abandonné et compté comme malformé (section 6).

```
Data non fiable ou contrôle :
 0      1      2             6             10                      fin-4
 +------+------+-------------+-------------+-----------------------+------------+
 |  4   | canal|     ack     |   ackBits   | message (0 ou 1)      | étiquette  |
 +------+------+-------------+-------------+-----------------------+------------+

Data fiable (canal 1) :
 0      1      2             6             10            14                fin-4
 +------+------+-------------+-------------+-------------+-----------------+------------+
 |  4   |  1   |     ack     |   ackBits   |     seq     | message         | étiquette  |
 +------+------+-------------+-------------+-------------+-----------------+------------+
```

Un **message** est son identifiant (u32) suivi de ses champs, sans nom de champ, sans taille et sans version :

```
 0             4
 +-------------+---------------------------------------
 |   id (u32)  | champs, dans l'ordre de KUGE_MESSAGE
 +-------------+---------------------------------------
```

L'identifiant est le hash FNV-1a 32 bits du nom du type. On part de 2166136261 ; pour chaque octet du nom, on fait un XOR avec l'octet puis on multiplie par 16777619, modulo 2^32. Les deux côtés MUST avoir un type de même nom et de mêmes champs, dans le même ordre. Un message, identifiant compris, MUST tenir dans 8174 octets sur le canal fiable et dans 8178 octets sur le canal non fiable ; `send` refuse le reste. (Sur UDP, bien moins : voir la section 8.)

À la réception :

1. Un paquet de type inconnu, un `Connect` reçu par un client ou dont le magic est faux, ou un `Data` ou `Goodbye` dont l'étiquette est fausse, est abandonné et compté comme malformé. Rien n'est répondu.
2. Un message dont l'identifiant n'a pas de gestionnaire est abandonné et compté (`unknownMessages`).
3. Un message tronqué, ou qui laisse des octets après son dernier champ, est abandonné et compté comme malformé. La connexion reste ouverte.
4. Deux types dont les noms ont le même hash ne peuvent pas être enregistrés sur un même endpoint : l'enregistrement lève une erreur.

## 5. Messages

Les messages du lobby et de la salle voyagent sur le canal fiable ; ceux de la réplication, sur le canal non fiable. Sens : C = client, L = lobby, S = salle.

### Lobby

| Message | Sens | Champs, dans l'ordre | Rôle |
| --- | --- | --- | --- |
| `JoinRoom` | C→L | roomType (chaîne), playerName (chaîne), protocol (u16) | demande une salle d'un type, que le lobby choisit |
| `CreateRoom` | C→L | roomType, roomName, playerName (chaînes), isPrivate (bool), options (chaîne), protocol (u16) | crée une salle nommée, avec des options que KUGE ne lit pas, et y place le client |
| `JoinNamedRoom` | C→L | roomId (u32), roomName, playerName (chaînes), protocol (u16) | entre dans la salle dont l'id et le nom correspondent |
| `ListRooms` | C→L | roomType (chaîne, vide pour tous) | demande les salles publiques |
| `RoomList` | L→C | total (u32), rooms (vecteur de `RoomInfo`) | réponse : 64 salles au plus, `total` les compte toutes |
| `RoomInfo` | dans `RoomList` | roomId (u32), roomType, name (chaînes), players (u32), maxPlayers (u32), options (chaîne), open (bool) | décrit une salle publique ; `open` est faux quand la salle est fermée aux nouveaux venus |
| `RoomAssigned` | L→C | roomId (u32), address (chaîne), port (u16), token (u64), tickRate (u32), playerId (u32), rejoinKey (u64) | où se trouve la salle, la clé pour y entrer, et ce qu'il faut pour y revenir (`RejoinRoom`) |
| `RejoinRoom` | C→L | roomId (u32), playerId (u32), rejoinKey (u64), protocol (u16) | un joueur dont la connexion à la salle est perdue redemande sa place ; le lobby répond `RoomAssigned` (nouveau token) ou `JoinRefused(UnknownRoom)`, la même réponse quoi qu'il y ait de faux |
| `JoinRefused` | L→C | reason (`JoinError`, u8) | la demande échoue |
| `LeaveRoom` | C→L | aucun | quitte la salle, ou renonce à en rejoindre une |
| `RoomClosed` | L→C | reason (`RoomEnd`, u8) | le lobby annonce la fin de la salle |

### Salle

| Message | Sens | Champs | Rôle |
| --- | --- | --- | --- |
| `Hello` | C→S | token (u64) | présente la clé donnée par le lobby |
| `Welcome` | S→C | networkId (u32), roomId (u32), tickRate (u32) | le joueur est admis ; `networkId` le désigne dans la salle |
| `Rejected` | S→C | reason (`HelloError`, u8) | le `Hello` est refusé |
| `RoomClosed` | S→C | reason (`RoomEnd`, u8) | la salle se termine |

### Réplication (canal non fiable)

| Message | Sens | Champs | Rôle |
| --- | --- | --- | --- |
| `SnapshotPacket` | S→C | schema (u32), tick (u32), baseTick (u32), inputAck (u32), part (u16), parts (u16), ops (octets : compteur u32 puis octets) | une part d'un instantané, voir la section 7 |
| `SnapshotAck` | C→S | tick (u32) | le client a appliqué cet instantané ; 0 signifie « envoie tout » |
| `InputPacket` | C→S | firstSequence (u32), count (u8), data (octets) | les dernières entrées du joueur, voir la section 7 |

### Valeurs des énumérations (u8)

| Énumération | Valeurs |
| --- | --- |
| `JoinError` | 0 UnknownRoomType, 1 Full, 2 AlreadyJoined, 3 RoomFailed, 4 Protocol, 5 InvalidName, 6 UnknownRoom, 7 InvalidOptions, 8 Started |
| `HelloError` | 0 UnknownToken, 1 Closing |
| `RoomEnd` | 0 GameOver, 1 Idle, 2 ServerStopping, 3 Kicked, 4 Error, 5 Victory |

Un nom de salle est valide s'il fait de 1 à 32 octets d'UTF-8 correct (RFC 3629), sans caractère de contrôle (U+0000 à U+001F, U+007F à U+009F) et sans espace ni tabulation à ses extrémités. Le lobby refuse les autres avec `JoinError::InvalidName`. Les `options` d'une salle font au plus 128 octets d'UTF-8 correct, sans caractère de contrôle ; le lobby refuse les autres avec `JoinError::InvalidOptions`.

## 6. Cycle de vie d'une connexion

Une connexion est « établie » quand l'autre côté a répondu, même sur UDP, **et que le serveur a vérifié que l'adresse du client reçoit ce qu'on lui envoie**. Le client choisit un sel aléatoire (u64) et le répète dans chaque `Connect`. Le serveur ne retient rien d'un `Connect` d'une adresse qu'il n'a pas vérifiée : il répond `Challenge` avec un cookie, et le client doit le lui renvoyer.

```
Client                                           Serveur
  |  (transport prêt : socket UDP, connexion TCP)    |  pair « Handshaking » (pas encore une connexion)
  |-- Connect(magic, version, sel, cookie 0) ------> |  23 octets
  |<-- Challenge(sel, cookie) -----------------------|  17 octets ; le serveur ne garde rien
  |-- Connect(magic, version, sel, cookie) --------> |  l'adresse a répondu
  |<-- Accept(sel, cookie) --------------------------|  17 octets ; connexion « Connected »
  |<=== Data (canaux fiable et non fiable) ========> |  chacun se termine par l'étiquette
  |-- Goodbye(étiquette) ---------------------------> |  connexion fermée (Remote)
```

Le **cookie** est un MAC : les 64 bits de SipHash-2-4, avec un secret que le serveur tire au hasard à son démarrage, de la fenêtre de temps courante (`cookieWindow`, 8 s), du sel et de l'adresse du pair (le texte que le transport donne, par exemple `10.0.0.7:4242`). Le serveur le recalcule pour le vérifier, pour la fenêtre courante et la précédente : il n'a rien à retenir. Seule une adresse qui reçoit les paquets du serveur peut le renvoyer. Sur TCP, l'adresse est déjà prouvée par la connexion : le serveur ne défie pas, et répond `Accept` d'emblée. Le sel du client et le cookie sont les deux moitiés de la clé des étiquettes.

**Client.** Il envoie `Connect` toutes les 0,25 s (`connectRetry`) une fois le transport prêt, et abandonne après 5 s (`connectTimeout`) avec la raison `Timeout`. À un `Challenge` dont le sel est le sien (reçu avant d'être connecté), il retient le cookie et renvoie `Connect` aussitôt : il en reçoit un autre si le cookie a expiré. Il considère la connexion établie à la réception d'un `Accept` dont le sel est le sien. Un paquet `Data` reçu avant l'`Accept` l'établit aussi, si son étiquette est celle du serveur : l'`Accept` s'est perdu, et le client a déjà la clé. Un `Reject` n'est pris en compte que pendant la connexion : il n'a pas d'étiquette, et ne doit pas pouvoir fermer une connexion établie.

**Serveur.** À l'arrivée d'un pair sur le transport, il crée une entrée « en attente » : ce n'est pas une connexion. On ne peut pas lui envoyer de message, elle n'est pas signalée, et elle ne compte pas dans `maxConnections`. Il en garde 256 au plus (`maxHalfOpen`) ; au-delà, la plus ancienne est abandonnée. Il répond comme suit à un `Connect` :

| Cas | Réponse du serveur |
| --- | --- |
| magic faux, ou paquet tronqué | rien (paquet malformé) |
| version différente de la sienne | `Reject`, puis fermeture (`Refused` côté client). La version est lue avant le reste : le format du `Connect` peut changer d'une version à l'autre. |
| plus de 64 `Connect` en une seconde de cette adresse (`connectsPerSecond`, adresse IP sans le port) | rien (compté dans `rateLimited`) |
| cookie absent, faux, d'une autre adresse, d'un autre sel, ou trop vieux (plus de deux fenêtres) | `Challenge(sel, cookie)` ; rien n'est retenu |
| cookie valide, serveur plein (`maxConnections` connexions établies) | `Reject`, puis fermeture |
| cookie valide | la connexion est établie, `Accept(sel, cookie)` |
| `Connect` répété avec le même sel et le même cookie, connexion établie | `Accept(sel, cookie)` de nouveau |
| `Connect` avec un autre sel ou un autre cookie sur une connexion établie | ignoré |

**Amplification.** Chaque réponse du serveur à un `Connect` est plus petite que lui : `Challenge` et `Accept` font 17 octets, `Reject` 5, et un `Connect` 23. Un attaquant qui usurpe une adresse fait recevoir à sa victime moins d'octets qu'il n'en a envoyé. Une entrée en attente qui n'envoie aucun `Connect` valide en 5 s est fermée sans bruit.

**Étiquette des paquets.** Une fois la connexion établie, chaque paquet `Data` et `Goodbye` se termine par une étiquette de 4 octets (section 4). Le récepteur la recalcule ; si elle diffère, il abandonne le paquet et le compte comme malformé, sans rafraîchir le délai de silence de la connexion. La clé est connue des deux extrémités et d'un tiers qui lit la poignée de main, pas d'un tiers qui ne la lit pas : l'étiquette arrête l'injection de paquets par un attaquant **hors du chemin**, qui connaît l'adresse et le format mais n'a pas vu `Connect` ni `Challenge`. Elle ne cache rien (rien n'est chiffré) et n'arrête pas un attaquant qui lit le trafic : voir la section 10.

**Vie d'une connexion.** Une connexion établie qui n'a rien envoyé depuis 1 s (`keepAlive`) envoie un paquet de contrôle ; elle en envoie aussi un dès qu'elle doit accuser réception. Un pair dont on n'a rien reçu depuis 10 s (`timeout`) est fermé avec la raison `Timeout`, après un `Goodbye`.

**Fin.** `Goodbye` est un paquet unique, sans accusé ; s'il se perd, le pair le découvre par le délai de silence. Chaque fermeture a une raison :

| Raison | Quand |
| --- | --- |
| `Local` | cet endpoint a appelé `disconnect` |
| `Remote` | le pair a envoyé `Goodbye` |
| `Timeout` | silence trop long, ou poignée de main jamais terminée (le client ne reçoit ni `Challenge` ni `Accept` en 5 s) |
| `Refused` | `Reject` reçu en se connectant |
| `Unreachable` | le transport a perdu le pair |

### Du lobby à la salle

```
Client                          Lobby (adresse connue)             Salle (son port)
  |-- JoinRoom ---------------------> |                                 |
  |<-- RoomAssigned(adresse, port, token)                                |
  |-- connexion UDP + Connect/Accept ------------------------------->   |
  |-- Hello(token) ---------------------------------------------->      |
  |<-- Welcome(networkId, roomId, tickRate) -----------------------      |
  |                      ... la partie ...                              |
  |<-- RoomClosed ------------------------------------------------      |
  | (le client est toujours connecté au lobby)                          |
```

Le token est un entier de 64 bits tiré au hasard par le lobby, qui le transmet à la salle avant de le donner au client. Il est à usage unique et vaut 10 s (`tokenTtl`). La salle garde 1 s un `Hello` dont elle ne connaît pas encore le token, le temps que le message du lobby arrive. Un `Hello` valide est suivi de `Welcome` ; sinon la salle répond `Rejected(UnknownToken)`, ou `Rejected(Closing)` si elle se termine. Une connexion à la salle qui ne dit pas `Hello` en 5 s (`helloTimeout`) est fermée.

La connexion au lobby reste ouverte pendant la partie. Quand la salle se termine, le client en est informé par `RoomClosed` et peut en demander une autre. Une salle sans joueur pendant `idleTimeout` (30 s par défaut) se ferme avec la raison `Idle`.

## 7. Réplication de l'état du monde

Le serveur est la seule source de vérité. Il envoie à chaque client les changements survenus depuis le dernier instantané que ce client a confirmé ; un instantané perdu coûte un instantané suivant plus gros, jamais un blocage.

```
Serveur                                                      Client
  |-- SnapshotPacket(tick 10, baseTick 0, part 0/1) ------->   |  applique, état[10]
  |<-- SnapshotAck(10) ---------------------------------------|
  |-- SnapshotPacket(tick 12, baseTick 10) ----X (perdu)       |
  |-- SnapshotPacket(tick 14, baseTick 10) ----------------->  |  applique, état[14]
  |<-- SnapshotAck(14) ---------------------------------------|
  |-- SnapshotPacket(tick 16, baseTick 14) ----------------->  |
```

**Entités et composants.** Une entité répliquée a un `NetworkId` (u32, attribué à partir de 1 et jamais réutilisé), un type (u16) et un propriétaire (u32, 0 pour personne). Ses composants sont numérotés par l'ordre d'enregistrement dans le registre, 64 au plus ; chacun est un bloc d'octets de 65535 octets au plus. Un composant a un mode : `Interpolated` (change tout le temps), `OnChange` (envoyé quand il change) ou `OnSpawn` (envoyé une fois, à l'apparition).

**Enregistrements (`ops`).** Un instantané est une suite d'enregistrements, chacun complet en lui-même :

| Genre | Octet 0 | Contenu |
| --- | --- | --- |
| Spawn | 1 | id (u32), type (u16), owner (u32), masque (u64), puis pour chaque bit du masque, par numéro croissant : taille (u16) et octets |
| Update | 2 | id (u32), changés (u64), retirés (u64), puis la taille (u16) et les octets de chaque composant changé |
| Destroy | 3 | id (u32) |

Le serveur émet `Spawn` pour une entité que le client n'a pas, `Update` pour celles dont un composant `Interpolated` ou `OnChange` a changé ou disparu, et `Destroy` pour celles qui ne sont plus répliquées. Un client MUST rejeter un instantané qui désigne un composant non enregistré, un `Update` d'une entité absente ou un genre inconnu.

**Parts.** Le serveur découpe les enregistrements en parts de 1100 octets au plus (`maxPartBytes`, la charge d'un datagramme que l'IP ne coupe pas), sans couper un enregistrement. Toutes les parts d'un instantané portent le même `tick`, `baseTick` et `inputAck`, `parts` leur nombre et `part` leur rang (0 à parts-1). Un enregistrement plus gros qu'une part n'est pas envoyé (compté dans `oversized`, avec un avertissement) ; le serveur retient alors, comme référence du client, l'état que celui-ci aura vraiment.

**Champs de `SnapshotPacket`.**

- `schema` : hash FNV-1a de la chaîne `nom:mode;` pour chaque composant du registre, dans l'ordre. Un client ignore sans rien signaler les instantanés d'un serveur dont le schéma diffère du sien.
- `tick` : le tick du serveur ; il croît à chaque envoi.
- `baseTick` : le tick de l'instantané sur lequel les enregistrements s'appuient ; 0 signifie « à partir de rien ».
- `inputAck` : le numéro de la dernière entrée de ce client que le serveur a appliquée.

**Accusés.** Après avoir appliqué un instantané complet, le client envoie `SnapshotAck(tick)`. Le serveur prend cet instantané comme nouvelle référence du client et oublie les plus anciens ; il en garde 64 au plus. Un accusé pour un tick inconnu ou plus ancien que la référence est ignoré. Si le client ne peut pas construire un instantané (`baseTick` qu'il n'a pas gardé, ou données incohérentes), il envoie `SnapshotAck(0)` et le serveur repart de rien. Un client MUST ignorer un instantané dont le `tick` n'est pas supérieur au dernier appliqué.

**Fréquence.** Le serveur envoie un instantané tous les 2 ticks par défaut (`sendInterval`, soit 30 par seconde à 60 Hz ; ou `sendRate` en instantanés par seconde, arrondi à un nombre entier de ticks), et immédiatement à un client à qui il n'en a encore envoyé aucun. Il SHOULD n'envoyer à un client que ce qu'il peut voir (filtre d'intérêt), sans que le protocole l'exige. Le client dessine les autres entités en retard de deux intervalles entre instantanés (l'intervalle étant le plus petit écart observé entre deux instantanés reçus) ; cela ne se lit pas sur le fil, et `interpolationDelay` le remplace par une durée fixe.

**Entrées du joueur.** Le client numérote ses entrées à partir de 1. Chaque `InputPacket` porte les 4 dernières (`redundancy`) : `firstSequence` est le numéro de la plus ancienne, `count` leur nombre, et `data` contient pour chacune sa longueur (u16) puis ses octets. Le serveur applique chaque entrée une fois et dans l'ordre. Si la suivante manque, il rejoue la dernière pour ce tick ; une entrée qui arrive après son tour est jetée. Il garde 64 entrées en attente au plus, et renvoie le numéro de la dernière appliquée dans `inputAck`, ce qui permet au client de retirer les entrées confirmées et de rejouer les autres sur l'état reçu (prédiction).

## 8. Fiabilité, ordre et taille des paquets

Chaque connexion a un canal fiable par sens. Il livre chaque message une fois et dans l'ordre sur un transport qui perd, répète et mélange les paquets. Le canal non fiable n'a ni numéro ni contrôle : un message y est envoyé une fois, peut se perdre, arriver deux fois ou dans le désordre.

**Numérotation.** Le premier message fiable d'un sens a le numéro 0 ; chaque suivant a le précédent + 1.

**Accusés.** Chaque paquet `Data`, de n'importe quel canal, porte deux champs :

- `ack` : le numéro du prochain message attendu. Tout message de numéro inférieur est livré.
- `ackBits` : le bit i vaut 1 si le message `ack + 1 + i` est déjà reçu (fenêtre de 32).

Un récepteur accuse même un doublon, car l'accusé précédent a pu se perdre. Il dépose en attente un message en avance, et le livre dès que les précédents sont là. Il compte comme doublon un numéro inférieur à `ack` et ne garde ni n'accuse un numéro supérieur ou égal à `ack + 256 + 32`.

**Envoi et renvoi.**

| Paramètre | Valeur par défaut |
| --- | --- |
| messages en vol | 256 (`window`) |
| messages en attente d'une place | 1024 (`maxQueued`) ; au-delà, `send` renvoie faux |
| délai avant le premier renvoi | 0,2 s, tant qu'aucun aller-retour n'est mesuré |
| délai de renvoi (RTO) | srtt + 4 × rttvar, borné à 0,03 s – 1,0 s |
| après chaque échec | le délai double, au plus 1,0 s |

L'aller-retour est mesuré seulement sur un message envoyé une fois : le premier échantillon donne srtt = échantillon et rttvar = échantillon / 2 ; ensuite rttvar = 0,75 × rttvar + 0,25 × |srtt − échantillon| puis srtt = 0,875 × srtt + 0,125 × échantillon. Ce sont les constantes de la RFC 6298. Un message envoyé une seule fois, dépassé par un message plus récent déjà accusé et sans réponse depuis max(0,03 s ; 1,5 × srtt), est renvoyé sans attendre le RTO.

**Sur TCP**, le transport est fiable : les messages restent numérotés, ordonnés et accusés, mais ne sont jamais renvoyés.

**Taille.** Un paquet fait 8192 octets au plus. Le protocole ne fragmente rien lui-même : `send` refuse un message trop grand, et seule la réplication découpe ses données en parts (section 7). Une part de 1100 octets de données, avec ses en-têtes, reste sous `DATAGRAM_PAYLOAD` (1200 octets).

**Limites connues.**

- Les numéros fiables sont des u32 comparés sans gestion du bouclage : ils sont sûrs pour 2^32 messages par sens et par connexion.
- 8192 octets dépassent la MTU habituelle d'Internet (environ 1500 octets). Un datagramme UDP plus grand dépend de la fragmentation IP, qui perd tout le datagramme si un fragment manque. Les parts de réplication tiennent sous `DATAGRAM_PAYLOAD` (1200 octets) par défaut ; un message du jeu, lui, SHOULD rester sous cette taille sur UDP s'il ne veut pas dépendre de la fragmentation IP, ce que le protocole ne vérifie pas (l'endpoint compte les paquets plus grands dans `bigPackets`). Un message fiable plus grand est renvoyé en entier : il n'est pas fragmenté par KUGE, et pas accusé par fragment.

## 9. Gestion d'erreurs et versionnage

Une donnée invalide est jetée et comptée dans les statistiques de l'endpoint ; elle ne ferme pas la connexion, sauf quand le transport lui-même est en défaut.

| Situation | Comportement |
| --- | --- |
| `Connect` au magic faux | abandonné, `malformed` ; aucune réponse |
| `Data` ou `Goodbye` dont l'étiquette est fausse | abandonné, `malformed` ; aucune réponse |
| trop de `Connect` d'une adresse | abandonné, `rateLimited` ; aucune réponse |
| `Connect` d'une adresse qui n'a pas prouvé la sienne | `Challenge` (compté dans `challenges`), rien de retenu |
| type de paquet inconnu, canal supérieur à 2, paquet tronqué | abandonné, `malformed` |
| identifiant de message sans gestionnaire | abandonné, `unknownMessages` |
| message tronqué ou trop long | abandonné, `malformed`, avertissement dans le journal |
| message fiable déjà livré | non relivré, accusé de nouveau, `duplicates` |
| version du protocole différente | `Reject`, raison `Refused` |
| trame TCP de longueur 0 ou supérieure à 8192 | connexion fermée |
| tampon d'envoi du transport plein | le pair est fermé (serveur), ou la connexion est perdue (client) |
| instantané d'un autre `schema` | ignoré, `wrongSchema` |
| instantané bâti sur un tick que le client n'a pas | ignoré, `SnapshotAck(0)`, `missingBaseline` |
| entrée de joueur mal formée | ignorée, `malformed` |

**Trois versions, contrôlées séparément.**

| Version | Où | Valeur | Contrôle |
| --- | --- | --- | --- |
| `protocol` | `Connect` (u16) | 2 | égalité exacte, sinon `Reject` |
| `MATCHMAKING_VERSION` | `JoinRoom`, `CreateRoom`, `JoinNamedRoom`, `RejoinRoom` (u16) | 3 | égalité exacte, sinon `JoinRefused(Protocol)` |
| `schema` | `SnapshotPacket` (u32) | hash du registre | égalité, sinon instantané ignoré |

Les messages n'ont pas de version propre : ajouter, retirer ou réordonner un champ d'un `KUGE_MESSAGE` casse la lecture, et le message est jeté comme malformé. Un tel changement dans le lobby ou la salle MUST s'accompagner d'une hausse de `protocol`. Le `schema` change quand les noms, les modes ou l'ordre des composants changent ; il ne change pas quand le format d'octets d'un composant change, et ce cas n'est pas détecté.

## 10. Considérations de sécurité

Le protocole ne chiffre rien et n'authentifie aucun joueur : il convient à un réseau de confiance (LAN, tests, parties entre amis), et une exposition sur Internet demande une protection ajoutée par-dessus.

- **Confidentialité et intégrité.** Tout circule en clair, token et nom du joueur compris. Seules les sommes de contrôle d'UDP et de TCP protègent contre une corruption accidentelle.
- **Usurpation sur UDP.** Une connexion ne se fait pas sur une adresse qui n'a pas répondu au `Challenge` : un `Connect` d'une adresse usurpée ne laisse aucune connexion, et n'occupe aucune des `maxConnections` places (au plus `maxHalfOpen` entrées en attente, les plus anciennes abandonnées). Ensuite, chaque paquet `Data` et `Goodbye` porte une étiquette de 32 bits sous une clé de 128 bits formée du sel du client et du cookie : un attaquant qui n'a pas lu la poignée de main doit deviner l'étiquette (une chance sur 2^32 par paquet), pas seulement l'adresse et le numéro de séquence. **Ce qui n'est pas couvert** : un attaquant qui lit le trafic (le sel et le cookie passent en clair) peut calculer des étiquettes, injecter, ou rejouer un paquet (il n'y a pas de protection contre le rejeu autre que la numérotation du canal fiable). Les `Reject` et `Challenge` n'ont pas d'étiquette. Le chiffrement et l'authentification des joueurs viennent plus tard, avec une dépendance.
- **Token.** Il a 64 bits tirés de `std::random_device`, sert une fois, vaut 10 s et n'ouvre qu'une salle. Il circule en clair dans `RoomAssigned` et `Hello` : qui l'observe peut l'utiliser avant le client.
- **Retour dans une salle.** `rejoinKey` a 64 bits tirés de `std::random_device`, ne change pas tant que le joueur est dans la salle, et circule en clair dans `RoomAssigned` comme le token. Elle est la seule preuve qu'une nouvelle connexion est le joueur qui a perdu la sienne : qui l'observe peut prendre sa place. Une salle ne garde la place d'un joueur que `reconnectSeconds` (0 par défaut : aucune).
- **Salle privée.** Elle n'est protégée que par son id, séquentiel à partir de 1, et par son nom, choisi par le créateur. Il ne faut pas les prendre pour un secret fort.
- **Amplification.** Un `Connect` de 23 octets reçoit un `Challenge` ou un `Accept` de 17 octets, ou un `Reject` de 5 : la réponse est plus petite que la demande. Les `Connect` d'une adresse sont comptés (64 par seconde par défaut, adresse IP sans le port) et ceux qui dépassent sont abandonnés sans réponse. Plusieurs joueurs derrière une même adresse (réseau local, NAT) se partagent ce compte.
- **Autorité du serveur.** Un client n'envoie que ses entrées et ses accusés ; le serveur n'accepte jamais d'état venu d'un client. Sans filtre d'intérêt, chaque client reçoit tout le monde, y compris ce qu'il ne devrait pas voir.
- **Noms.** Ce protocole ne définit pas de validation du nom d'un joueur : un jeu qui l'affiche MUST le traiter comme une entrée non fiable.

Les limites suivantes bornent ce qu'un pair peut coûter à un autre :

| Ressource | Limite |
| --- | --- |
| connexions par endpoint | 64 ; dans une salle, 2 × joueurs max + 4 |
| pairs qui n'ont pas prouvé leur adresse (entrées en attente) | 256 (`maxHalfOpen`) |
| `Connect` pris d'une adresse IP en une seconde | 64 (`connectsPerSecond`) |
| adresses dont les `Connect` sont comptés | 4096 |
| poignée de main, ou `Hello` | 5 s |
| validité d'un cookie | de `cookieWindow` à 2 × `cookieWindow` (8 à 16 s) |
| silence d'un pair | 10 s |
| messages fiables en vol, en attente | 256, 1024 |
| numéro fiable accepté en réception | inférieur à `ack + 288` |
| trame TCP | 8192 octets |
| part de réplication (enregistrements) | 1100 octets (`maxPartBytes`) |
| datagramme à ne pas dépasser sur UDP | 1200 octets (`DATAGRAM_PAYLOAD`) |
| vecteur lu | au plus autant d'éléments que d'octets restants |
| entrées en attente par client | 64 |
| instantanés incomplets en attente, états gardés (client) | 16, 32 |
| composants répliqués | 64 |

## Annexe A. Exemples d'octets

Tous les nombres sont en little-endian ; les octets sont en hexadécimal.

Dans ces exemples, le sel du client vaut `0x1122334455667788` et le cookie `0xA1A2A3A4A5A6A7A8` (une valeur choisie pour l'exemple : le vrai cookie est un MAC). Les étiquettes sont celles de paquets envoyés par le client avec cette clé.

```
Connect, version 2, sel, cookie 0 (23 octets)
  01 | 4B 55 47 45 | 02 00 | 88 77 66 55 44 33 22 11 | 00 00 00 00 00 00 00 00

Challenge pour ce sel, avec le cookie (17 octets)
  06 | 88 77 66 55 44 33 22 11 | A8 A7 A6 A5 A4 A3 A2 A1

Connect avec le cookie (23 octets)
  01 | 4B 55 47 45 | 02 00 | 88 77 66 55 44 33 22 11 | A8 A7 A6 A5 A4 A3 A2 A1

Accept pour ce sel, avec le cookie (17 octets)
  02 | 88 77 66 55 44 33 22 11 | A8 A7 A6 A5 A4 A3 A2 A1

Paquet de contrôle (keep-alive), ack 5, ackBits 0, étiquette (14 octets)
  04 02 | 05 00 00 00 | 00 00 00 00 | 02 33 5C E4

Hello, token 0x0102030405060708, dans un Data fiable (seq 0, ack 0, ackBits 0), étiquette (30 octets)
  04 01 | 00 00 00 00 | 00 00 00 00 | 00 00 00 00 | 4B 31 5C F5 | 08 07 06 05 04 03 02 01 | D7 42 CC 65

SnapshotAck, tick 300, dans un Data non fiable (ack 0, ackBits 0), étiquette (22 octets)
  04 00 | 00 00 00 00 | 00 00 00 00 | 4A 45 41 3C | 2C 01 00 00 | 5D 63 14 8D
```

### Identifiants des messages (FNV-1a 32 bits du nom)

| Message | Identifiant | Octets sur le fil |
| --- | --- | --- |
| `JoinRoom` | 0x76887FA6 | A6 7F 88 76 |
| `CreateRoom` | 0xFF731462 | 62 14 73 FF |
| `JoinNamedRoom` | 0x940587F1 | F1 87 05 94 |
| `ListRooms` | 0x51A77B17 | 17 7B A7 51 |
| `RoomList` | 0x2EE55876 | 76 58 E5 2E |
| `RoomInfo` | 0xDCF0AC42 | 42 AC F0 DC |
| `RoomAssigned` | 0xEC7581C2 | C2 81 75 EC |
| `RejoinRoom` | 0x3AAD1EA3 | A3 1E AD 3A |
| `JoinRefused` | 0xDD5D9C17 | 17 9C 5D DD |
| `LeaveRoom` | 0xA07C3AE3 | E3 3A 7C A0 |
| `RoomClosed` | 0x04CF4606 | 06 46 CF 04 |
| `Hello` | 0xF55C314B | 4B 31 5C F5 |
| `Welcome` | 0x588A5993 | 93 59 8A 58 |
| `Rejected` | 0xF050C067 | 67 C0 50 F0 |
| `SnapshotPacket` | 0x1ECDF189 | 89 F1 CD 1E |
| `SnapshotAck` | 0x3C41454A | 4A 45 41 3C |
| `InputPacket` | 0xB7EDDDD7 | D7 DD ED B7 |

## Annexe B. Références

- [RFC 2119](https://www.rfc-editor.org/rfc/rfc2119) : MUST, SHOULD, MAY.
- [RFC 768](https://www.rfc-editor.org/rfc/rfc768) : UDP. [RFC 9293](https://www.rfc-editor.org/rfc/rfc9293) : TCP.
- [RFC 3629](https://www.rfc-editor.org/rfc/rfc3629) : UTF-8.
- [RFC 6298](https://www.rfc-editor.org/rfc/rfc6298) : calcul du RTO, dont KUGE reprend les constantes (1/8, 1/4, 4).

## Annexe C. Sources dans le code

Ce mémo décrit le commit `3c53df8` (branche `feat/arena3d-effects`, 2026-10-19). Les identifiants de la section 5 viennent de `KUGE_MESSAGE` ; les valeurs de l'annexe A ont été calculées avec la fonction FNV-1a de `Wire.hpp`, réécrite à part, et non en exécutant le moteur.

| Sujet | Fichiers |
| --- | --- |
| sérialisation, messages | `modules/core/src/Serializer.hpp`, `modules/net/src/Wire.hpp` |
| paquets, session | `modules/net/src/Endpoint.cpp`, `modules/net/src/Endpoint.hpp` |
| canal fiable | `modules/net/src/Reliable.cpp` |
| transports, trames TCP | `modules/net/src/SocketTransport.cpp`, `modules/net/src/Transport.hpp` |
| lobby et salles | `modules/net/src/Matchmaking.hpp`, `modules/server/src/Lobby.cpp`, `modules/server/src/RoomScene.cpp`, `modules/server/src/ServerConfig.hpp` |
| réplication, entrées | `modules/replication/src/ReplicationProtocol.hpp`, `WorldState.cpp`, `ReplicationServer.cpp`, `ReplicationClient.cpp`, `Input.hpp` |
