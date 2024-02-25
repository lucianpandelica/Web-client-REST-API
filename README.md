
# Web client - communication with a REST API

## Structura de functionare a clientului:

Am implementat clientul folosind multiplexare cu epoll, pentru a gestiona
inchiderile de conexiune din partea serverului. Practic, epoll urmareste
in permanenta doi file descriptori: sockfd curent aferent conexiunii cu
serverul (strict pentru a sti cand s-a inchis conexiunea), respectiv stdin
pentru input de la utilizator.
Avand in vedere ca citirea raspunsurilor aferente cererilor trimise la server
se realizeaza imediat dupa trimiterea cererilor, in interiorul functiilor
handler, nu putem intampina cazul in care primim mesaje de la server care
creeaza un eveniment asociat sockfd in epoll, ci doar cazul inchiderii
conexiunii.

Astfel, am ales sa folosesc un camp in structura ce contine datele clientului
(ce va fi detaliata mai jos) care sa retina starea conexiunii la un moment dat.
Cand serverul inchide conexiunea, setam acest camp la STATE_DISCONNECTED, iar
daca dorim sa trimitem o cerere la server iar campul respectiv are aceasta
valoare, restabilim conexiunea (practic se creeaza o noua conexiune, cu un
sockfd nou), iar valoarea campului devine STATE_CONNECTED.

Totusi, deoarece timpul de inchidere a conexiunii din partea serverului in caz de
inactivitate este foarte mic (5 secunde), exista cazul urmator:

1. introducem o comanda si completam datele necesare ca input;
2. apoi intram in functia handler corespondenta comenzii, unde verificam starea
	conexiunii si aceasta este deschisa;
3. imediat dupa acest moment conexiunea se inchide, iar cererea nu se mai
	poate trimite.

Astfel, am decis sa inchid conexiunea manual dupa primirea raspunsului fiecarei
cereri, pentru a evita acest caz. Pentru o testare automata si o conexiune buna
la internet acesta nu ar fi aparut (am testat folosind checkerul pus la
dispozitie), dar aparea frecvent la testarea manuala.

Am pastrat structura folosind multiplexarea cu epoll in implementare, doar ca
nu mai exista posibilitatea aparitiei de evenimente din partea serverului acum.
Se poate reveni la implementarea initiala prin stergerea apelurilor functiei
'handle_conn_closed' din interiorul fiecarei functii handler asociata unei
comenzi.


Dupa cum am mentionat mai sus, am folosit o structura 'Client' pentru a retine
toate datele asociate acestuia, precum:
- sockfd-ul curent
- token-ul de acces la biblioteca
- cookie-urile de sesiune (desi se primeste mereu unul singur)
- starea conexiunii

## Gestionarea comenzilor:

Folosim functia 'getline' pentru a citi input-ul de la utilizator, pana la
caracterul '\n' inclusiv. De fiecare data cand se introduce o comanda,
comparam continutul bufferului cu numele de comenzi cunoscute si directionam
executia in functie de portivire. Daca nu recunoastem comanda, afisam un mesaj
de eroare.

* comanda 'register'

Citim username-ul si parola si le verificam validitatea (adica sa nu contina
spatii), iar apoi construim obiectul JSON care contine aceste date si trimitem
un POST request catre server, la adresa specificata in enunt.
Citim apoi raspunsul primit din partea serverului, il interpretam si oferim
feedback utilizatorului.

* comanda 'login'

Pentru inceput, verificam daca suntem deja autentificati intr-un cont (daca avem
un cookie de sesiune). In caz afirmativ, intoarcem un mesaj de eroare, altfel
preluam datele contului de la utilizator analog comenzii 'register'. Construim
obiectul JSON si trimitem un POST request la server.
De asemenea, citim raspunsul, iar daca cererea a fost valida extragem cookie-ul
de sesiune din header, pe care il retinem (doar dupa ce il stergem pe cel vechi,
daca acesta exista).

* comanda 'enter_library'

Verificam daca utilizatorul este logat (are cookie de sesiune) pentru a valida
cererea de acces la biblioteca. In caz afirmativ, trimitem un GET request si
retinem token-ul primit ca raspuns - daca cererea a fost valida - (dupa ce il
stergem pe cel vechi, daca acesta exista).

* comanda 'get_books'

Verificam daca utilizatorul are acces la biblioteca (are un token). In caz afirmativ
trimitem un GET request si printam vectorul de obiecte primit ca raspuns, daca
cererea a fost valida.

* comanda 'get_book'

Verificam analog accesul la biblioteca, preluam id-ul cartii de la utilizator
si verificam validitatea inputului (sa se fi introdus un numar intreg).
Construim apoi URL-ul aferent ID-ului si trimitem un POST request la server,
interpretand apoi raspunsul la cerere si afisand datele cartii.

* comanda 'add_book'

Verificam accesul la biblioteca, apoi preluam atributele cartii. Verificam
validitatea numarului de pagini, construim obiectul JSON cu datele cartii si
trimitem un POST request la server, avand drept corp obiectul creat. Citim
raspunsul si oferim feedback utilizatorului.

* comanda 'delete_book'

Verificam accesul la biblioteca si procedam analog functiei 'get_book', dar
trimitem un DELETE request.

* comanda 'logout'

Verificam daca suntem deja autentificati intr-un cont (avem cookie de sesiune).
In caz negativ, intoarcem un mesaj de eroare. Altfel, trimitem un GET request
la adresa specificata in enunt, citim raspunsul, iar daca operatia a reusit
stergem cookie-ul actual de sesiune si token-ul de acces la biblioteca, pentru
ca un utilizator sa nu poata accesa datele altui cont.

* comanda 'exit'

Eliberam resursele alocate clientului (cookie, token), inchidem socketul,
inchidem epoll.


_Feedback catre utilizator:_

> Dupa primirea fiecarui raspuns la o cerere, folosim functia 'give_feedback' ce
extrage status code-ul intors de server. In functie de prima cifra a acestuia,
identificam daca cererea s-a realizat cu succes sau nu. Functia primeste si un
sir de caractere ca argument pe care sa il scrie catre utilizator in cazul in
care operatia a reusit.
>
> Daca s-a intampinat o eroare (alte coduri decat 2xx), folosim functia
'print_error' care extrage obiectul JSON primit in corpul raspunsului si afiseaza
valoarea campului 'error'.


## Biblioteca parser JSON:

Am folosit biblioteca "parson" in implementarea temei, plecand de la exemplele
puse la dispozitie de autorii acesteia in documentatie. Am ales functiile
pentru a ajuta la realizarea unei implementari robuste si sigure din punct
de vedere al alocarii memoriei.


## Functiile de trimitere a cererilor din 'requests.c':

Am generalizat functia pentru cereri GET astfel incat sa accepte si cereri
DELETE. De asemenea, adaugarea cookie-ului de sesiune si a token-ului de
autentificare se face automat, prin simpla pasare ca argumente a campurilor
aferente acestora din structura clientului. Daca unul / ambele nu exista,
nu se vor adauga, deoarece campurile aferente din structura vor fi setate
ca NULL. 


## Mentiuni:

Am plecat de la scheletul laboratorului 9 (Protocolul HTTP), peste care am
implementat clientul folosind functiile puse la dispozitie in fisierele
'requests.c', respectiv 'helpers.c'.
De asemenea, am folosit un fisier header 'w_epoll.h' cu wrappere pentru
epoll preluat de la cursul "Sisteme de Operare".


## Bibliografie:

https://pcom.pages.upb.ro/labs/
https://jwt.io/introduction
https://www.postman.com/
https://github.com/kgabis/parson

