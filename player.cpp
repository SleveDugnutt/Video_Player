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
    #include <libavutil/opt.h>
    #include <libswscale/swscale.h>
    #include <libswresample/swresample.h>
}
#include "decode_frames.hpp"

int main(int argc, char *argv[]){
    const char *input = argv[1];
    int ret = 0;
    AVFormatContext *inputFmtCtx = NULL;
    ret = avformat_open_input(&inputFmtCtx, input, NULL, NULL);
    if (ret < 0){
        std::cout << "Could not open input file" << std::endl;
    }
    ret = avformat_find_stream_info(inputFmtCtx, NULL);
    if (ret < 0){
        std::cout << "Could not find stream info" << std::endl;
    }
    int video_stream_index = -1;
    int audio_stream_index = -1;
    const AVCodec *videoDecoder = NULL;
    const AVCodec *audioDecoder = NULL;
    AVCodecContext *videoDecodeCtx = NULL;
    AVCodecContext *audioDecodeCtx = NULL;
    for (int i=0; i<(int)inputFmtCtx->nb_streams; ++i){
        AVStream *stream = inputFmtCtx->streams[i];
        AVCodecParameters *codecpar = stream->codecpar;
        if (codecpar->codec_type == AVMEDIA_TYPE_VIDEO){
            video_stream_index = i;
            videoDecoder = avcodec_find_decoder(codecpar->codec_id);
            videoDecodeCtx = avcodec_alloc_context3(videoDecoder);
            avcodec_parameters_to_context(videoDecodeCtx, codecpar);
            videoDecodeCtx->time_base = stream->time_base;
            videoDecodeCtx->framerate = stream->avg_frame_rate;
            avcodec_open2(videoDecodeCtx, videoDecoder, NULL);
        }
        if (codecpar->codec_type == AVMEDIA_TYPE_AUDIO){
            audio_stream_index = i;
            audioDecoder = avcodec_find_decoder(codecpar->codec_id);
            audioDecodeCtx = avcodec_alloc_context3(audioDecoder);
            avcodec_parameters_to_context(audioDecodeCtx, codecpar);
            audioDecodeCtx->time_base = stream->time_base;
            avcodec_open2(audioDecodeCtx, audioDecoder, NULL);
        }
    }
    int WIDTH = videoDecodeCtx->width;
    int HEIGHT = videoDecodeCtx->height;
    int channels = audioDecodeCtx->channels;
    AVPacket *packet = av_packet_alloc();
    AVFrame *frame = av_frame_alloc();
    /*
    AVFrame *rgbframe = av_frame_alloc();
    rgbframe->width = WIDTH;
    rgbframe->height = HEIGHT;
    rgbframe->format = AV_PIX_FMT_RGB24;
    uint8_t *buf = (uint8_t*) av_malloc(av_image_get_buffer_size(AV_PIX_FMT_RGB24, WIDTH, HEIGHT, 1));
    ret = av_image_fill_arrays(rgbframe->data, rgbframe->linesize, buf, AV_PIX_FMT_RGB24, WIDTH, HEIGHT, 1);
    SwsContext *torgb = sws_getContext(WIDTH, HEIGHT, videoDecodeCtx->pix_fmt,
                                       WIDTH, HEIGHT, AV_PIX_FMT_RGB24,
                                       SWS_BICUBIC, NULL, NULL, NULL);
    */
    //double scale = 0.6;
    int Window_WIDTH = 1280;
    int Window_HEIGHT = 720;
    AVPixelFormat dst_pix_fmt = AV_PIX_FMT_RGB24;
    SwsContext *scaler = sws_getContext(WIDTH, HEIGHT, videoDecodeCtx->pix_fmt,
                                        Window_WIDTH, Window_HEIGHT, dst_pix_fmt,
                                        SWS_BICUBIC, NULL, NULL, NULL);
    int dst_samplerate = 44100;
    AVSampleFormat dst_smp_fmt = AV_SAMPLE_FMT_S16;
    SwrContext *resampler = swr_alloc_set_opts(NULL, 
                                               audioDecodeCtx->channel_layout,
                                               dst_smp_fmt,
                                               dst_samplerate,
                                               audioDecodeCtx->channel_layout,
                                               audioDecodeCtx->sample_fmt,
                                               audioDecodeCtx->sample_rate,
                                               0, 
                                               NULL);
    swr_init(resampler);
    AVFrame *dstframe = av_frame_alloc();
    dstframe->width = Window_WIDTH;
    dstframe->height = Window_HEIGHT;
    dstframe->format = dst_pix_fmt;
    ret = av_frame_get_buffer(dstframe, 0);
    uint8_t *buf = (uint8_t*) av_malloc(av_image_get_buffer_size(dst_pix_fmt, Window_WIDTH, Window_HEIGHT, 1));
    ret = av_image_fill_arrays(dstframe->data, dstframe->linesize, buf, dst_pix_fmt, Window_WIDTH, Window_HEIGHT, 1);
    AVFrame *audioframe = av_frame_alloc();
    audioframe->channel_layout = audioDecodeCtx->channel_layout;
    audioframe->sample_rate = dst_samplerate;
    audioframe->format = dst_smp_fmt;
    ret = av_frame_get_buffer(audioframe, 0);
    //collect frames
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
    SDL_AudioSpec want, have;
    SDL_zero(want);
    SDL_zero(have);
    want.freq = dst_samplerate;
    want.channels = channels;
    want.format = AUDIO_S16SYS;
    dev = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
    SDL_PauseAudioDevice(dev, 0);
    bool running = true;
    bool playing = true;
    //start playing
    while (running){
        auto T1 = std::chrono::high_resolution_clock::now();
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
            ret = av_read_frame(inputFmtCtx, packet);
            if (ret < 0){
                running = false;
                break;
            }
            AVStream *stream = inputFmtCtx->streams[packet->stream_index];
            double timebase = av_q2d(stream->time_base);
            if (stream->codecpar->codec_type == video_stream_index){
                ret = avcodec_send_packet(videoDecodeCtx, packet);
                while (ret >= 0){
                    ret = avcodec_receive_frame(videoDecodeCtx, frame);
                    if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) break;
                    else if (ret >= 0){
                        double video_time = (double)frame->best_effort_timestamp * timebase * 1000;
                        while (true){
                            auto T2 = std::chrono::high_resolution_clock::now();
                            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(T2 - T1).count();
                            if (elapsed >= video_time) break;
                        }
                        sws_scale(scaler, 
                                  frame->data, 
                                  frame->linesize,
                                  0, 
                                  frame->height, 
                                  dstframe->data, 
                                  dstframe->linesize);
                        SDL_UpdateTexture(texture, 
                                          &rect, 
                                          dstframe->data[0], 
                                          dstframe->linesize[0]);
                        SDL_RenderClear(renderer);
                        SDL_RenderCopy(renderer, 
                                       texture, 
                                       NULL, 
                                       &rect);
                        SDL_RenderPresent(renderer);
                    }
                }
            }
            if (stream->codecpar->codec_type == audio_stream_index){
                ret = avcodec_send_packet(audioDecodeCtx, packet);
                while (ret >= 0){
                    ret = avcodec_receive_frame(audioDecodeCtx, frame);
                    if (ret >= 0){
                        int dst_samples = frame->channels * av_rescale_rnd(swr_get_delay(resampler, frame->sample_rate) + frame->nb_samples,
                                                                           dst_samplerate, 
                                                                           frame->sample_rate,
                                                                           AV_ROUND_UP);
                        uint8_t *audiobuf = NULL;
                        ret = av_samples_alloc(&audiobuf, 
                                               NULL, 
                                               1, 
                                               dst_samples,
                                               dst_smp_fmt, 
                                               1);
                        dst_samples = frame->channels * swr_convert(resampler, 
                                                                    &audiobuf, 
                                                                    dst_samples,
                                                                    (const uint8_t**) frame->data, 
                                                                    frame->nb_samples);
                        ret = av_samples_fill_arrays(audioframe->data, 
                                                     audioframe->linesize, 
                                                     audiobuf,
                                                     1, 
                                                     dst_samples, 
                                                     dst_smp_fmt, 
                                                     1);
                        SDL_QueueAudio(dev, audioframe->data[0], audioframe->linesize[0]);    
                    }
                }
                av_frame_unref(frame);
            }
        }       
    }
    av_packet_free(&packet);
    av_frame_free(&frame);
    //av_frame_free(&rgbframe);
    av_frame_free(&dstframe);
    av_frame_free(&audioframe);
    av_freep(&buf);
    //sws_freeContext(torgb);
    sws_freeContext(scaler);
    swr_free(&resampler);
    avformat_free_context(inputFmtCtx);
    avcodec_free_context(&videoDecodeCtx);
    avcodec_free_context(&audioDecodeCtx);
    SDL_CloseAudioDevice(dev);
    SDL_DestroyTexture(texture);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}