#include <stdio.h>

extern "C"
{
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
#include <libavutil/imgutils.h>
#include <libswresample/swresample.h>
#include <SDL.h>
}

#define MAX_AUDIO_FRAME_SIZE 192000

static Uint32 audio_len;
static Uint8* audio_chunk;
static Uint8* audio_pos;

void fill_audio(void* udata, Uint8* stream, int len) {
	SDL_memset(stream, 0, len);
	if (audio_len == 0)
		return;
	len = (len > audio_len ? audio_len : len);
	SDL_MixAudio(stream, audio_pos, len, SDL_MIX_MAXVOLUME);
	audio_pos += len;
	audio_len -= len;
}


int main(int argc, char** argv)
{
	char filepath[] = "sintel.ts";
	AVFormatContext* pFormatCtx = NULL;
	AVStream* pStream = NULL, *pStreamAu = NULL;
	AVCodec* pCodec = NULL, *pCodecAu = NULL;
	AVCodecContext* pCodecCtx = NULL, *pCodecCtxAu = NULL;
	struct SwsContext* imgConvertCtx = NULL;
	AVFrame* pFrameYUV, *pFrame;
	AVPacket* pkt = NULL;
	SwrContext* pSwrCtx = NULL;
	int ret = 0;
	unsigned int videoindex = -1, audioindex = 1;

	int screen_w = 0, screen_h = 0;
	SDL_Window* screen;
	SDL_Renderer* sdlRenderer;
	SDL_Texture* sdlTexture;
	SDL_Rect sdlRect;
	SDL_AudioSpec wanted_spec;

	ret = avformat_open_input(&pFormatCtx, filepath, NULL, NULL);
	if (ret != 0) {
		printf("could not open input stream\n");
		return -1;
	}
	ret = avformat_find_stream_info(pFormatCtx, NULL);
	if (ret < 0) {
		printf("could not find stream infomation\n");
		return -1;
	}
	printf("nb_streams = %u\n", pFormatCtx->nb_streams);
	for (unsigned int i = 0; i < pFormatCtx->nb_streams; ++i) {
		if (videoindex == -1 && pFormatCtx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
			videoindex = i;
			continue;
		}
		else if (audioindex == -1 && pFormatCtx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
			audioindex = i;
			continue;
		}
	}
	printf("video index = %d,,,,, audio index = %d\n", videoindex, audioindex);
	if (videoindex == -1 || audioindex == -1) {
		printf("do not find a video or audio stream \n");
		return -1;
	}
	pStream = pFormatCtx->streams[videoindex];
	pCodec = avcodec_find_decoder(pStream->codecpar->codec_id);
	if (!pCodec) {
		printf("could not find codec\n");
		return -1;
	}
	pCodecCtx = avcodec_alloc_context3(NULL);
	if (!pCodecCtx) {
		printf("could not allocate context\n");
		return -1;
	}
	avcodec_parameters_to_context(pCodecCtx, pStream->codecpar);
	ret = avcodec_open2(pCodecCtx, pCodec, NULL);
	if (ret < 0) {
		printf("could not open codec\n");
		return -1;
	}
	printf("decodec name: %s\n", pCodec->name);
	pStreamAu = pFormatCtx->streams[audioindex];
	pCodecAu = avcodec_find_decoder(pStreamAu->codecpar->codec_id);
	if (!pCodecAu) {
		printf("could not find audio codec\n");
		return -1;
	}
	pCodecCtxAu = avcodec_alloc_context3(NULL);
	if (!pCodecCtxAu) {
		printf("could not allocate audio codec context\n");
		return -1;
	}
	avcodec_parameters_to_context(pCodecCtxAu, pStreamAu->codecpar);
	ret = avcodec_open2(pCodecCtxAu, pCodecAu, NULL);
	if (ret < 0) {
		printf("could not open audio codec\n");
		return -1;
	}
	printf("audio decodec name: %s\n", pCodecAu->name);
	pkt = (AVPacket*)av_malloc(sizeof(AVPacket));
	if (!pkt) {
		printf("malloc packet error\n");
		return -1;
	}
	uint64_t outChannelLayout = AV_CH_LAYOUT_STEREO;
	int outNbSamples = pCodecCtxAu->frame_size;
	AVSampleFormat outSampleFmt = AV_SAMPLE_FMT_S16;
	int outSampleRate = 44100;
	int outChannels = av_get_channel_layout_nb_channels(outChannelLayout);
	int outBufferSize = av_samples_get_buffer_size(NULL, outChannels, outNbSamples, outSampleFmt, 1);
	uint8_t* outAudioBuffer = (uint8_t*)av_malloc(MAX_AUDIO_FRAME_SIZE*2);

	pFrame = av_frame_alloc();

	imgConvertCtx = sws_getContext(pCodecCtx->width, pCodecCtx->height, pCodecCtx->pix_fmt, pCodecCtx->width, pCodecCtx->height, AV_PIX_FMT_YUV420P, SWS_BICUBIC, NULL, NULL, NULL);
	if (!imgConvertCtx) {
		printf("could not swsConvertCtx\n");
		return -1;
	}
	pFrameYUV = av_frame_alloc();
	int buffsize = av_image_get_buffer_size(AV_PIX_FMT_YUV420P, pCodecCtx->width, pCodecCtx->height, 1);
	printf("buffer size: %d\n", buffsize);
	uint8_t* outBuffer = (uint8_t*)av_malloc(buffsize);

	av_image_fill_arrays(pFrameYUV->data, pFrameYUV->linesize, outBuffer, AV_PIX_FMT_YUV420P, pCodecCtx->width, pCodecCtx->height, 1);

	if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER)) {
		printf("could not init SDL :%s\n", SDL_GetError());
		return -1;
	}
	wanted_spec.freq = outSampleRate;
	wanted_spec.format = AUDIO_S16SYS;
	wanted_spec.channels = outChannels;
	wanted_spec.silence = 0;
	wanted_spec.samples = outNbSamples;
	wanted_spec.callback = fill_audio;
	wanted_spec.userdata = pCodecCtxAu;
	if (SDL_OpenAudio(&wanted_spec, NULL) < 0) {
		printf("could not open audio\n");
		return -1;
	}
	int64_t inChannelLayout = av_get_default_channel_layout(pCodecCtxAu->channels);
	pSwrCtx = swr_alloc_set_opts(pSwrCtx, outChannelLayout, outSampleFmt, outSampleRate, inChannelLayout, pCodecCtxAu->sample_fmt, pCodecCtxAu->sample_rate, 0, NULL);
	swr_init(pSwrCtx);

	SDL_PauseAudio(0);

	screen_w = pCodecCtx->width;
	screen_h = pCodecCtx->height;
	screen = SDL_CreateWindow("simplest ffmpeg player window", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, screen_w, screen_h, SDL_WINDOW_OPENGL);
	if (!screen) {
		printf("could not create window\n");
		return -1;
	}
	sdlRenderer = SDL_CreateRenderer(screen, -1, 0);
	sdlTexture = SDL_CreateTexture(sdlRenderer, SDL_PIXELFORMAT_IYUV, SDL_TEXTUREACCESS_STREAMING, pCodecCtx->width, pCodecCtx->height);
	sdlRect.x = 0;
	sdlRect.y = 0;
	sdlRect.w = screen_w;
	sdlRect.h = screen_h;
	while (av_read_frame(pFormatCtx, pkt) >= 0) {
		if (pkt->stream_index == videoindex) {
			ret = avcodec_send_packet(pCodecCtx, pkt);
			printf("111  ret= %d\n", ret);
			if (ret < 0) {
				printf("send a packet for decoding error");
				return -1;
			}
			while (ret >= 0) {
				ret = avcodec_receive_frame(pCodecCtx, pFrame);
				printf("222  ret= %d\n", ret);
				if (ret < 0) {
					printf("3333 %d----%d\n", AVERROR(EAGAIN), AVERROR_EOF);
					if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
						break;
					}
					printf("video decode error\n");
					return -1;
				}
				sws_scale(imgConvertCtx, (const uint8_t* const*)pFrame->data, pFrame->linesize, 0, pCodecCtx->height, pFrameYUV->data, pFrameYUV->linesize);
				SDL_UpdateYUVTexture(sdlTexture, &sdlRect, pFrameYUV->data[0], pFrameYUV->linesize[0], pFrameYUV->data[1], pFrameYUV->linesize[1], pFrameYUV->data[2], pFrameYUV->linesize[2]);
				SDL_RenderClear(sdlRenderer);
				SDL_RenderCopy(sdlRenderer, sdlTexture, NULL, &sdlRect);
				SDL_RenderPresent(sdlRenderer);
				SDL_Delay(40);
			}
		}
		else if (pkt->stream_index == audioindex) {
			ret = avcodec_send_packet(pCodecCtxAu, pkt);
			printf("555  ret= %d\n", ret);
			if (ret < 0) {
				printf("send a packet for decoding error");
				return -1;
			}
			while (ret >= 0) {
				ret = avcodec_receive_frame(pCodecCtxAu, pFrame);
				printf("666  ret= %d\n", ret);
				if (ret < 0) {
					printf("777 %d----%d\n", AVERROR(EAGAIN), AVERROR_EOF);
					if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
						break;
					}
					printf("audio decode error\n");
					return -1;
				}
				swr_convert(pSwrCtx, &outAudioBuffer, MAX_AUDIO_FRAME_SIZE, (const uint8_t**)pFrame->data, pFrame->nb_samples);
				while (audio_len > 0)
					SDL_Delay(1);
				audio_chunk = (uint8_t*)outAudioBuffer;
				audio_pos = audio_chunk;
				audio_len = outBufferSize;
			}
		}
		
	}
	SDL_CloseAudio();
	SDL_Quit();
	swr_free(&pSwrCtx);
	av_packet_free(&pkt);
	av_frame_free(&pFrameYUV);
	av_frame_free(&pFrame);
	avcodec_close(pCodecCtx);
	avcodec_close(pCodecCtxAu);
	avformat_close_input(&pFormatCtx);

	return 0;
}