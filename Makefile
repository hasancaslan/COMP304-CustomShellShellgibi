shellymake: shelly.c
	gcc -lreadline -o shelly shelly.c
	mv shelly /usr/bin

wttrinmake: wttrin.c
	gcc -o wttrin wttrin.c
	mv wttrin /usr/bin