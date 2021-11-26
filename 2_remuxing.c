// based on https://ffmpeg.org/doxygen/trunk/remuxing_8c-example.html
#include <libavutil/timestamp.h>
#include <libavformat/avformat.h>

int main(int argc, char **argv) {
    // 1、
    // AVFormatContext是FFmpeg中打开文件必备的一个结构体。
    // 之前介绍过，格式Format是音视频的一个核心概念，所以在FFmpeg里你需要经常与AVFormatContext打交道。
    // 因为一般不是直接操作解封装器Demuxer和封装器Muxer，而是通过AVFormatContext来操作它们。

    // 2、
    // AVFormatContext持有的是传递过程中的数据，这些数据在整个传递路径上都存在，或者都可以复用，
    // AVInputFormat/AVOutputFormat中包含的是动作，包含着如何解析得到的这些数据。

    // 3、AVInputFormat
    // 解封装器Demuxer，正式的结构体是AVInputFormat，其实是一个接口，功能是对封装后的格式容器解开获得编码后的音视频的工具。
    // 简单说，就是拆包工具。
    // 我们所知道的各种多媒体格式，例如MP4、MP3、FLV等格式的读取，都有AVInputFormat的具体实现。
    // demuxer的种类很多，而且是可配置的，demuxer有多少，可以看一下demuxer_list.c文件，太多了，不一一列举了。我们举一个mp4 demuxer的例子。
    // 你可以看到AVInputFormat提供的是类似接口一样的功能，而ff_mov_demuxer是其的一个具体实现。
    // FFmpeg其实本身的逻辑并不复杂，只是由于支持的格式特别丰富，所以代码才如此多。
    // 如果我们先把大部分格式忽略掉，重点关注FFmpeg对其中几个格式的实现，可以更好理解FFmpeg。

    // 4、AVOutputFormat
    // 封装器 Muxer，对应的结构体是AVOutputFormat，也是一个接口，功能是对编码后的音视频封装进格式容器的工具。
    // 简单说，就是打包工具。
    // 跟解封装器 Demuxer类似，也是MP4、MP3、FLV等格式的实现，差别是封装器 Muxer用于输出。
    // 与demuxer类似，muxer的种类很多，可以看一下muxer_list.c文件。
    AVFormatContext *input_format_context = NULL, *output_format_context = NULL;
    AVPacket packet;
    const char *in_filename, *out_filename;
    int ret, i;
    int stream_index = 0;
    int *streams_list = NULL;
    int number_of_streams = 0;
    int fragmented_mp4_options = 0;

    if (argc < 3) {
        printf("You need to pass at least two parameters.\n");
        return -1;
    } else if (argc == 4) {
        fragmented_mp4_options = 1;
    }

    in_filename = argv[1];
    out_filename = argv[2];

    if ((ret = avformat_open_input(&input_format_context, in_filename, NULL, NULL)) < 0) {
        fprintf(stderr, "Could not open input file '%s'", in_filename);
        goto end;
    }
    if ((ret = avformat_find_stream_info(input_format_context, NULL)) < 0) {
        fprintf(stderr, "Failed to retrieve input stream information");
        goto end;
    }

    //创建输出媒体文件的AVFormatContext
    avformat_alloc_output_context2(&output_format_context, NULL, NULL, out_filename);
    if (!output_format_context) {
        fprintf(stderr, "Could not create output context\n");
        ret = AVERROR_UNKNOWN;
        goto end;
    }

    number_of_streams = input_format_context->nb_streams;
    streams_list = av_mallocz_array(number_of_streams, sizeof(*streams_list));

    if (!streams_list) {
        ret = AVERROR(ENOMEM);
        goto end;
    }

    for (i = 0; i < input_format_context->nb_streams; i++) {
        AVStream *out_stream;
        AVStream *in_stream = input_format_context->streams[i];
        AVCodecParameters *in_codecpar = in_stream->codecpar;
        if (in_codecpar->codec_type != AVMEDIA_TYPE_AUDIO && in_codecpar->codec_type != AVMEDIA_TYPE_VIDEO &&
            in_codecpar->codec_type != AVMEDIA_TYPE_SUBTITLE) {
            streams_list[i] = -1;
            continue;
        }
        streams_list[i] = stream_index++;
        out_stream = avformat_new_stream(output_format_context, NULL);
        if (!out_stream) {
            fprintf(stderr, "Failed allocating output stream\n");
            ret = AVERROR_UNKNOWN;
            goto end;
        }
        ret = avcodec_parameters_copy(out_stream->codecpar, in_codecpar);
        if (ret < 0) {
            fprintf(stderr, "Failed to copy codec parameters\n");
            goto end;
        }
    }

    // https://ffmpeg.org/doxygen/trunk/group__lavf__misc.html#gae2645941f2dc779c307eb6314fd39f10
    //打印 format 详情
    av_dump_format(output_format_context, 0, out_filename, 1);

    // unless it's a no file (we'll talk later about that) write to the disk (FLAG_WRITE)
    // but basically it's a way to save the file to a buffer so you can store it
    // wherever you want.
    if (!(output_format_context->oformat->flags & AVFMT_NOFILE)) {
        ret = avio_open(&output_format_context->pb, out_filename, AVIO_FLAG_WRITE);
        if (ret < 0) {
            fprintf(stderr, "Could not open output file '%s'", out_filename);
            goto end;
        }
    }
    AVDictionary *opts = NULL;

    if (fragmented_mp4_options) {
        // https://developer.mozilla.org/en-US/docs/Web/API/Media_Source_Extensions_API/Transcoding_assets_for_MSE
        av_dict_set(&opts, "movflags", "frag_keyframe+empty_moov+default_base_moof", 0);
    }

    // https://ffmpeg.org/doxygen/trunk/group__lavf__encoding.html#ga18b7b10bb5b94c4842de18166bc677cb
    // 写入输出文件的媒体头部信息
    ret = avformat_write_header(output_format_context, &opts);
    if (ret < 0) {
        fprintf(stderr, "Error occurred when opening output file\n");
        goto end;
    }

    while (1) {
        AVStream *in_stream, *out_stream;
        //读取媒体文件中每一帧数据，这是未解码之前的帧
        ret = av_read_frame(input_format_context, &packet);
        if (ret < 0)
            break;
        in_stream = input_format_context->streams[packet.stream_index];
        if (packet.stream_index >= number_of_streams || streams_list[packet.stream_index] < 0) {
            av_packet_unref(&packet);
            continue;
        }
        packet.stream_index = streams_list[packet.stream_index];
        out_stream = output_format_context->streams[packet.stream_index];
        /* copy packet */
        packet.pts = av_rescale_q_rnd(packet.pts, in_stream->time_base, out_stream->time_base,
                                      AV_ROUND_NEAR_INF | AV_ROUND_PASS_MINMAX);
        packet.dts = av_rescale_q_rnd(packet.dts, in_stream->time_base, out_stream->time_base,
                                      AV_ROUND_NEAR_INF | AV_ROUND_PASS_MINMAX);
        packet.duration = av_rescale_q(packet.duration, in_stream->time_base, out_stream->time_base);
        // https://ffmpeg.org/doxygen/trunk/structAVPacket.html#ab5793d8195cf4789dfb3913b7a693903
        packet.pos = -1;

        //https://ffmpeg.org/doxygen/trunk/group__lavf__encoding.html#ga37352ed2c63493c38219d935e71db6c1
        //写入输出文件的帧信息，此帧信息已经调整了帧与帧之间的关联了。
        ret = av_interleaved_write_frame(output_format_context, &packet);
        if (ret < 0) {
            fprintf(stderr, "Error muxing packet\n");
            break;
        }
        av_packet_unref(&packet);
    }
    //https://ffmpeg.org/doxygen/trunk/group__lavf__encoding.html#ga7f14007e7dc8f481f054b21614dfec13
    //写入输出文件的媒体尾部信息
    av_write_trailer(output_format_context);
    end:
    //关闭媒体文件
    avformat_close_input(&input_format_context);
    /* close output */
    if (output_format_context && !(output_format_context->oformat->flags & AVFMT_NOFILE))
        avio_closep(&output_format_context->pb);

    avformat_free_context(output_format_context);
    av_freep(&streams_list);

    if (ret < 0 && ret != AVERROR_EOF) {
        fprintf(stderr, "Error occurred: %s\n", av_err2str(ret));
        return 1;
    }

    return 0;
}

