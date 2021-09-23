#include <iostream>
#include <vector>
#include <chrono>
#include <thread>
#include <mutex>
#define SDL_MAIN_HANDLED
#include <SDL2/SDL.h>
extern "C"{
    #include <libavformat/avformat.h>
    #include <libavcodec/avcodec.h>
    #include <libavutil/imgutils.h>
    #include <libswscale/swscale.h>
    #include <libswresample/swresample.h>
}

int main(int argc, char *argv[]){
    const char *input = argv[1];
    AVFormatContext *inputFmtContxt = NULL;
    //AVFormatContext *audioFmtContxt = NULL;
    const AVCodec *video_decoder = NULL;
    const AVCodec *audio_decoder = NULL;
    AVCodecContext *video_decoderContxt = 0;
    AVCodecContext *audio_decoderContxt = 0;
    int ret = 0;
    int video_stream_index = -1;
    int audio_stream_index = -1;
    ret = avformat_open_input(&inputFmtContxt, input, NULL, NULL);
    //ret = avformat_open_input(&audioFmtContxt, input, NULL, NULL);
    if (ret < 0){
        std::cout << "Could not open input video" << std::endl;
    }
    ret = avformat_find_stream_info(inputFmtContxt, NULL);
    if (ret < 0){
        std::cout << "Could not find stream info" << std::endl;
    }
    //prepare decoder
    for (int i=0; i<(int)inputFmtContxt->nb_streams; ++i){
        AVStream *in_stream = inputFmtContxt->streams[i];
        AVCodecParameters *in_par = in_stream->codecpar;
        if (in_par->codec_type == AVMEDIA_TYPE_VIDEO){
            video_stream_index = i;
            video_decoder = avcodec_find_decoder(in_par->codec_id);
            video_decoderContxt = avcodec_alloc_context3(video_decoder);
            avcodec_parameters_to_context(video_decoderContxt, in_par);
            avcodec_open2(video_decoderContxt, video_decoder, NULL);
        }
        if (in_par->codec_type == AVMEDIA_TYPE_AUDIO){
            audio_stream_index = i;
            audio_decoder = avcodec_find_decoder(in_par->codec_id);
            audio_decoderContxt = avcodec_alloc_context3(audio_decoder);
            avcodec_parameters_to_context(audio_decoderContxt, in_par);
            avcodec_open2(audio_decoderContxt, audio_decoder, NULL);
        }
    }
    int delay;
    double framerate = av_q2d(video_decoderContxt->framerate);
    double spf = 1 / framerate;
    //double timebase = av_q2d(video_decoderContxt->time_base);
    int WIDTH = video_decoderContxt->width;
    int HEIGHT = video_decoderContxt->height;
    double scale = 0.6;
    int Window_WIDTH = WIDTH * scale;
    int Window_HEIGHT = HEIGHT * scale;
    AVPixelFormat dst_pix_fmt = AV_PIX_FMT_RGB24;
    SwsContext *torgba = sws_getContext(WIDTH, HEIGHT, video_decoderContxt->pix_fmt,
                                        Window_WIDTH, Window_HEIGHT, dst_pix_fmt,
                                        SWS_BICUBIC, NULL, NULL, NULL);
    AVPacket *packet = av_packet_alloc();
    AVFrame *frame = av_frame_alloc();
    AVFrame *dstframe = av_frame_alloc();
    dstframe->width = Window_WIDTH;
    dstframe->height = Window_HEIGHT;
    dstframe->format = dst_pix_fmt;
    ret = av_frame_get_buffer(dstframe, 0);
    uint8_t *buf = (uint8_t*) av_malloc(av_image_get_buffer_size(dst_pix_fmt, Window_WIDTH, Window_HEIGHT, 1));
    ret = av_image_fill_arrays(dstframe->data, dstframe->linesize, buf, dst_pix_fmt, Window_WIDTH, Window_HEIGHT, 1);
    //SDL part
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) < 0){
        std::cout << "Cound not initialize SDL2" << std::endl;
    }
    SDL_Window *window = SDL_CreateWindow(input,
                                          SDL_WINDOWPOS_CENTERED,
                                          SDL_WINDOWPOS_CENTERED,
                                          Window_WIDTH, Window_HEIGHT, 0);
    SDL_Renderer *renderer = SDL_CreateRenderer(window, -1, 0);
    SDL_Texture *texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGB24,
                                             SDL_TEXTUREACCESS_STREAMING,
                                             Window_WIDTH, Window_HEIGHT);
    SDL_Rect rect;
    rect.w = Window_WIDTH;
    rect.h = Window_HEIGHT;
    rect.x = 0;
    rect.y = 0;
    SDL_Event event;
    SDL_AudioDeviceID dev;
    SDL_AudioSpec wanted, spec;
    SDL_zero(wanted);
    SDL_zero(spec);
    wanted.freq = audio_decoderContxt->sample_rate;
    wanted.channels = audio_decoderContxt->channels;
    dev = SDL_OpenAudioDevice(NULL, 0, &wanted, &spec, 0);
    SDL_PauseAudioDevice(dev, 0);
    bool running = true;
    bool playing = true;
    while (running){
        while (playing){
            while (SDL_PollEvent(&event)){
                if (event.type == SDL_QUIT){
                    running = false;
                    playing = false;
                    break;
                }
                if (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_ESCAPE){
                    running = false;
                    playing = false;
                    break;
                }
            }
            ret = av_read_frame(inputFmtContxt, packet);
            if (ret < 0){
                running = false;
                playing = false;
                break;
            }
            AVStream *stream = inputFmtContxt->streams[packet->stream_index];
            if (stream->codecpar->codec_type == video_stream_index){
                ret = avcodec_send_packet(video_decoderContxt, packet);
                while (ret >= 0){
                    ret = avcodec_receive_frame(video_decoderContxt, frame);
                    if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF){
                        break;
                    }
                    if (ret >= 0){
                        delay = spf * (1 + 0.5 * frame->repeat_pict);
                        sws_scale(torgba, frame->data, frame->linesize, 0, frame->height,
                                  dstframe->data, dstframe->linesize);
                        SDL_UpdateTexture(texture, &rect, dstframe->data[0], dstframe->linesize[0]);
                        SDL_RenderClear(renderer);
                        SDL_RenderCopy(renderer, texture, NULL, &rect);
                        SDL_RenderPresent(renderer);
                        SDL_Delay(1000 * delay);
                    }
                }
                av_frame_unref(frame);
            }
            if (stream->codecpar->codec_type == audio_stream_index){
                ret = avcodec_send_packet(audio_decoderContxt, packet);
                while (ret >= 0){
                    ret = avcodec_receive_frame(audio_decoderContxt, frame);
                    if (ret >= 0){
                        SDL_QueueAudio(dev, frame->data[0], frame->linesize[0]);       
                    }
                }
                av_frame_unref(frame);
            }
            av_packet_unref(packet);
        }       
    }
    av_frame_free(&frame);
    av_frame_free(&dstframe);
    av_packet_free(&packet);
    avcodec_free_context(&video_decoderContxt);
    avcodec_free_context(&audio_decoderContxt);
    avformat_free_context(inputFmtContxt);
    av_freep(&buf);
    sws_freeContext(torgba);
    SDL_CloseAudioDevice(dev);
    SDL_DestroyTexture(texture);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}