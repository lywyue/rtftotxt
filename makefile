readrtf:main.cpp
	g++ -std=c++17 -g $< -o $@ -liconv
clean:
	rm -fr readrtf
