#include <stdio.h>

extern "C"
{
#include<libavcodec/avcodec.h>
#include<SDL.h>
//#include<SDL_main.h>
};
//#pragma comment(lib, "avcodec.lib")
//#pragma comment(lib ,"SDL2.lib")
//#pragma comment(lib, "SDL2main.lib")
//#define SDL_MAIN_HANDLED
int main1(int argc, char** argv)
{
	printf("===== %s\n", avcodec_configuration());
	if (SDL_Init(SDL_INIT_VIDEO)) {
		printf("could not init SDL = %s\n", SDL_GetError());
	}
	else {
		printf("Sccess init SDL\n");
	}
	return 0;
}