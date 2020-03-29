shellgibimake: shellgibi.c
	gcc -o shellgibi shellgibi.c -lreadline
	mv shellgibi /usr/bin

wttrinmake: wttrin.c
	gcc -o wttrin wttrin.c
	mv wttrin /usr/bin