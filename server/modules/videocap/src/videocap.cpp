/************************************************************************
 **
 ** 作者：许振坪
 ** 日期：2017-05-03
 ** 博客：http://blog.csdn.net/benkaoya
 ** 描述：读取IPC音视频流数据示例代码
 **
 ************************************************************************/
extern "C"{
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>

#include <sys/time.h>
#include <sys/stat.h>
// #include <sys/types.h>

#include "libavcodec/avcodec.h"
#include "libavdevice/avdevice.h"
#include "libavformat/avformat.h"
#include "libavfilter/avfilter.h"
#include "libavutil/avutil.h"
#include "libswscale/swscale.h"
#include "libavutil/pixdesc.h"
#include "libavutil/time.h"
}

#include <opencv2/opencv.hpp>

#include "onvif_comm.h"
#include "onvif_dump.h"

#include "thread_pool.h"
#include "webcamdata.h"
#include "videocap.h"

static std::vector<std::string> camURIs;


/************************************************************************
**函数:ONVIF_GetSystemDateAndTime
**功能:获取设备的系统时间
**参数:
        [in] DeviceXAddr - 设备服务地址
**返回
        0: 成功  1: 失败
**备注
    1). 对于IPC摄像头, OSD打印的时间是其LocalDateTime
************************************************************************/
int ONVIF_GetSystemDateAndTime(const char *DeviceXAddr)
{
    int result = 0;
    struct soap *soap = NULL;
    struct _tds__GetSystemDateAndTime         req;
    struct _tds__GetSystemDateAndTimeResponse rep;

    SOAP_ASSERT(NULL != DeviceXAddr);

    SOAP_ASSERT(NULL != (soap = ONVIF_soap_new(SOAP_SOCK_TIMEOUT)));

    ONVIF_SetAuthInfo(soap, USERNAME, PASSWORD);

    memset(&req, 0x00, sizeof(req));
    memset(&rep, 0x00, sizeof(rep));
    result = soap_call___tds__GetSystemDateAndTime(soap, DeviceXAddr, NULL, &req, &rep);
    SOAP_CHECK_ERROR(result, soap, "GetSystemDateAndTime");

    dump_tds__GetSystemDateAndTime(&rep);

EXIT:

    if (NULL != soap) {
        ONVIF_soap_delete(soap);
    }

    return result;
}

/************************************************************************
**函数:ONVIF_GetHostTimeZone
**功能:获取主机的时间信息
**参数:
        [out] TZ    - 返回的时区信息
        [in] sizeTZ - TZ缓存大小
**返回: 无
**备注
************************************************************************/
static void ONVIF_GetHostTimeZone(char *TZ, int sizeTZ)
{
    char timezone[20] = {0};

#ifdef WIN32

    TIME_ZONE_INFORMATION TZinfo;
    GetTimeZoneInformation(&TZinfo);
    sprintf(timezone, "GMT%c%02d:%02d",  (TZinfo.Bias <= 0) ? '+' : '-', labs(TZinfo.Bias) / 60, labs(TZinfo.Bias) % 60);

#else

    FILE *fp = NULL;
    char time_fmt[32] = {0};

    fp = popen("date +%z", "r");
    fread(time_fmt, sizeof(time_fmt), 1, fp);
    pclose(fp);

    if( ((time_fmt[0] == '+') || (time_fmt[0] == '-')) &&
        isdigit(time_fmt[1]) && isdigit(time_fmt[2]) && isdigit(time_fmt[3]) && isdigit(time_fmt[4]) ) {
            sprintf(timezone, "GMT%c%c%c:%c%c", time_fmt[0], time_fmt[1], time_fmt[2], time_fmt[3], time_fmt[4]);
    } else {
        strcpy(timezone, "GMT+08:00");
    }

#endif

    if (sizeTZ > strlen(timezone)) {
        strcpy(TZ, timezone);
    }

    return;
}

/************************************************************************
**函数:ONVIF_SetSystemDateAndTime
**功能:根据客户端主机当前时间,校时IPC的系统时间
**参数:
        [in] DeviceXAddr - 设备服务地址
**返回:
       0: 成功  非0:失败
**备注:
       1).对于IPC摄像头,OSD打印的时间是其本地时间(本地事件跟时区息息相关),
          设置时间时一定要注意时区的正确性
************************************************************************/
int ONVIF_SetSystemDateAndTime(const char *DeviceXAddr)
{
    int result = 0;
    struct soap *soap = NULL;
    struct _tds__SetSystemDateAndTime           req;
    struct _tds__SetSystemDateAndTimeResponse   rep;

    char TZ[20];                                                                // ÓÃÓÚ»ñÈ¡¿Í»§¶ËÖ÷»úµÄÊ±ÇøÐÅÏ¢£¨Èç"GMT+08:00"£©
    time_t t;                                                                   // ÓÃÓÚ»ñÈ¡¿Í»§¶ËÖ÷»úµÄUTCÊ±¼ä
    struct tm tm;

    SOAP_ASSERT(NULL != DeviceXAddr);
    SOAP_ASSERT(NULL != (soap = ONVIF_soap_new(SOAP_SOCK_TIMEOUT)));

    ONVIF_GetHostTimeZone(TZ, DIM(TZ));                                         // 获取客户端主机的时区信息

    t = time(NULL);                                                             // 获取客户端主机的UTC时间
    TZ[3] = '-';                                                                // 修改时区信息,按照 GMT+08:00 计算会少八个小时,但按照GMT-08:00计算会多八个小时,尚不清楚原因
#ifdef WIN32
    gmtime_s(&tm, &t);
#else
    gmtime_r(&t, &tm);
#endif

    memset(&req, 0x00, sizeof(req));
    memset(&rep, 0x00, sizeof(rep));
    req.DateTimeType      = tt__SetDateTimeType__Manual;
    req.DaylightSavings   = xsd__boolean__false_;
    req.TimeZone          = (struct tt__TimeZone *)ONVIF_soap_malloc(soap, sizeof(struct tt__TimeZone));
    req.UTCDateTime       = (struct tt__DateTime *)ONVIF_soap_malloc(soap, sizeof(struct tt__DateTime));
    req.UTCDateTime->Date = (struct tt__Date *)ONVIF_soap_malloc(soap, sizeof(struct tt__Date));
    req.UTCDateTime->Time = (struct tt__Time *)ONVIF_soap_malloc(soap, sizeof(struct tt__Time));

    req.TimeZone->TZ              = TZ;                                         // 设置本地时区(IPC的OSD显示的时间就是本地时间)
    req.UTCDateTime->Date->Year   = tm.tm_year + 1900;                          // 设置UTC时间(注意不是本地时间)
    req.UTCDateTime->Date->Month  = tm.tm_mon + 1;
    req.UTCDateTime->Date->Day    = tm.tm_mday;
    req.UTCDateTime->Time->Hour   = tm.tm_hour;
    req.UTCDateTime->Time->Minute = tm.tm_min;
    req.UTCDateTime->Time->Second = tm.tm_sec;

    ONVIF_SetAuthInfo(soap, USERNAME, PASSWORD);
    result = soap_call___tds__SetSystemDateAndTime(soap, DeviceXAddr, NULL, &req, &rep);
    SOAP_CHECK_ERROR(result, soap, "SetSystemDateAndTime");

EXIT:

    if (NULL != soap) {
        ONVIF_soap_delete(soap);
    }

    return result;
}

/************************************************************************
 **函数：open_rtsp
 **功能：从RTSP获取音视频流数据
 ************************************************************************/
void* open_rtsp(void *arg)
{
	WebCamData *camData = (WebCamData *)arg;

	int             ret, got_picture;
	int             video_st_index = -1;
	int             audio_st_index = -1;
	AVFormatContext *ifmt_ctx = NULL; //保存视频流的信息
	AVPacket        *packet;
	char            errbuf[64];
	//
	AVCodecContext* pCodecCtx; //ffmpeg解码类的类成员
	AVCodec *pCodec; //解码器指针
	AVFrame *pAvFrame, *pFrameBGR; //多媒体帧，保存解码后的数据帧

	av_register_all();                                                          // Register all codecs and formats so that they can be used.
	avformat_network_init();                                                    // Initialization of network components

	if ((ret = avformat_open_input(&ifmt_ctx, camData->webCamURI.c_str(), 0, NULL)) < 0) {            // Open the input file for reading.
		printf("Could not open input file '%s' (error '%s')\n", camData->webCamURI.c_str(), av_make_error_string(errbuf, sizeof(errbuf), ret));
		goto EXIT;
	}

	if ((ret = avformat_find_stream_info(ifmt_ctx, NULL)) < 0) {                // Get information on the input file (number of streams etc.).
		printf("Could not open find stream info (error '%s')\n", av_make_error_string(errbuf, sizeof(errbuf), ret));
		goto EXIT;
	}

	for (int i = 0; i < ifmt_ctx->nb_streams; i++) {                                // dump information
		av_dump_format(ifmt_ctx, i, camData->webCamURI.c_str(), 0);
	}

	for (int i = 0; i < ifmt_ctx->nb_streams; i++) {                                // find video stream index
		switch(ifmt_ctx->streams[i]->codec->codec_type) {
			case AVMEDIA_TYPE_AUDIO: audio_st_index = i; break;
			case AVMEDIA_TYPE_VIDEO: video_st_index = i; break;
			default: break;
		}
	}
	if (-1 == video_st_index) {
		printf("No H.264 video stream in the input file\n");
		goto EXIT;
	}

	pCodecCtx = ifmt_ctx->streams[video_st_index]->codec; //得到一个指向视频流的上下文指针
	pCodec = avcodec_find_decoder(pCodecCtx->codec_id); //到该格式的解码器
	if (pCodec == NULL) {
		printf("Cant't find the decoder !\n"); //寻找解码器
		return NULL;
	}
	if (avcodec_open2(pCodecCtx, pCodec, NULL) < 0) { //打开解码器
		printf("Can't open the decoder !\n");
		return NULL;
	}

	pAvFrame = av_frame_alloc(); //分配帧存储空间
	pFrameBGR = av_frame_alloc(); //存储解码后转换的RGB数据

	// 保存BGR，opencv中是按BGR来保存的
	int size;
	uint8_t *out_buffer;
	size = avpicture_get_size(AV_PIX_FMT_BGR24, pCodecCtx->width, pCodecCtx->height);
	out_buffer = (uint8_t *)av_malloc(size);
	avpicture_fill((AVPicture *)pFrameBGR, out_buffer, AV_PIX_FMT_BGR24, pCodecCtx->width, pCodecCtx->height);

	packet = (AVPacket*)malloc(sizeof(AVPacket));
	printf("-----------输出文件信息---------\n");
	av_dump_format(ifmt_ctx, 0, camData->webCamURI.c_str(), 0);
	printf("------------------------------");

	struct SwsContext *img_convert_ctx;
	img_convert_ctx = sws_getContext(pCodecCtx->width,
			pCodecCtx->height, 
			pCodecCtx->pix_fmt, 
			pCodecCtx->width, 
			pCodecCtx->height,
			AV_PIX_FMT_BGR24, //设置sws_scale转换格式为BGR24，这样转换后可以直接用OpenCV显示图像了
			SWS_BICUBIC, 
			NULL, NULL, NULL);

	for(;;){
		do {
			ret = av_read_frame(ifmt_ctx, packet);                                // read frames
		} while (ret == AVERROR(EAGAIN));
		if (ret < 0) {
			printf("Could not read frame (error '%s')\n", av_make_error_string(errbuf, sizeof(errbuf), ret));
			break;
		}

		if (packet->stream_index == video_st_index) {                               // video frame
			// printf("Video Packet size = %d\n", packet->size);
			
			ret = avcodec_decode_video2(pCodecCtx, pAvFrame, &got_picture, packet);
			if (ret < 0){
				printf("Decode Error.（解码错误）\n");
				return NULL;
			}
			if (got_picture){
				//YUV to RGB
				sws_scale(img_convert_ctx, 
						(const uint8_t* const*)pAvFrame->data, 
						pAvFrame->linesize, 
						0, 
						pCodecCtx->height,
						pFrameBGR->data, 
						pFrameBGR->linesize);

				cv::Mat mRGB(cv::Size(pCodecCtx->width, pCodecCtx->height), CV_8UC3);
				mRGB.data =(uchar*)pFrameBGR->data[0];//注意不能写为：(uchar*)pFrameBGR->data
				camData->loadImg(mRGB);
			}
		} else if(packet->stream_index == audio_st_index) {                         // audio frame
			// printf("Audio Packet size = %d\n", packet->size);
		} else {
			// printf("Unknow Packet size = %d\n", packet->size);
		}

		av_packet_unref(packet);
	}

EXIT:

	if (ifmt_ctx != NULL) {
		avformat_close_input(&ifmt_ctx);
		ifmt_ctx = NULL;
	}
	if(out_buffer != NULL){
		av_free(out_buffer);
		out_buffer = NULL;
	}
	if(pFrameBGR != NULL){
		av_free(pFrameBGR);
		pFrameBGR = NULL;
	}
	if(pAvFrame != NULL){
		av_free(pAvFrame);
		pAvFrame = NULL;
	}
	if(pCodecCtx != NULL){
		avcodec_close(pCodecCtx);
		pCodecCtx = NULL;
	}

	if(img_convert_ctx != NULL){
		sws_freeContext(img_convert_ctx);
		img_convert_ctx = NULL;
	}

	return NULL;
}

/************************************************************************
 **函数：saveToVideo
 **功能：编码视频并保存
 ************************************************************************/
void* save_to_video(void *arg)
{
	// sleep(30);
	int single_video_long = 10;  // min
	WebCamData *camData = (WebCamData *)arg;

	AVOutputFormat *ofmt = NULL;
	AVFormatContext *ifmt_ctx = NULL, *ofmt_ctx = NULL;
	AVPacket pkt;
	int ret;
	int videoindex = -1;
	int audioindex = -1;
	int frame_index = 0;
	char errbuf[64];

	if ((ret = avformat_open_input(&ifmt_ctx, camData->webCamURI.c_str(), 0, NULL)) < 0) {            // Open the input file for reading.
		printf("Could not open input file '%s' (error '%s')\n", camData->webCamURI.c_str(), av_make_error_string(errbuf, sizeof(errbuf), ret));
		// goto end;
	}
	if ((ret = avformat_find_stream_info(ifmt_ctx, NULL)) < 0) {                // Get information on the input file (number of streams etc.).
		printf("Could not open find stream info (error '%s')\n", av_make_error_string(errbuf, sizeof(errbuf), ret));
		// goto end;
	}
	av_dump_format(ifmt_ctx, 0, camData->webCamURI.c_str(), 0);

	for (int i = 0; i < ifmt_ctx->nb_streams; i++) {                                // find video stream index
		switch(ifmt_ctx->streams[i]->codec->codec_type) {
			case AVMEDIA_TYPE_AUDIO: audioindex = i; break;
			case AVMEDIA_TYPE_VIDEO: videoindex = i; break;
			default: break;
		}
	}
	if (-1 == videoindex) {
		printf("No H.264 video stream in the input file\n");
		return NULL;
	}

	while(1){
		int64_t start_time = 0;
		int cur_begin_time_min = 0;
		std::string cur_dir_name, old_dir_name, out_filename;

		start_time = av_gettime();

		time_t rawtime;
		struct tm * timeinfo;
		// struct tm
		// {	int tm_sec; int tm_min; int tm_hour;
		// 	int tm_mday; int tm_mon; int tm_year;
		// 	int tm_wday;  // Day of week. [0-6]
		// 	int tm_yday;  // Days in year.[0-365]
		// 	int tm_isdst; };  // DST. [-1/0/1]
		time ( &rawtime );
		timeinfo = localtime ( &rawtime );
		// asctime() 将参数timeptr所指的tm结构中的信息转换成真实世界所使用的时间日期表示方法，然后将结果以字符串形态返回
		printf ( "Current local time and date: %s", asctime (timeinfo) );
		cur_begin_time_min = timeinfo->tm_min;

		// create new directory
		cur_dir_name += std::to_string(timeinfo->tm_year+1900);	cur_dir_name += "_";
		cur_dir_name += std::to_string(timeinfo->tm_mon+1);	cur_dir_name += "_";
		cur_dir_name += std::to_string(timeinfo->tm_mday); cur_dir_name += "-";
		cur_dir_name += std::to_string(camData->camIndex);
		if(access(cur_dir_name.c_str(), 0) < 0){  // 参数2为0时表示检查文件夹是否存在,存在返回0,不存在返回-1
			mkdir(cur_dir_name.c_str(), 0755);
		}

		// remove old directory
		old_dir_name += std::to_string(timeinfo->tm_mon==0 ? timeinfo->tm_year+1900 - 1 : timeinfo->tm_year+1900);
		old_dir_name += "_";
		old_dir_name += std::to_string(timeinfo->tm_mon==0 ? 12 : timeinfo->tm_mon);
		old_dir_name += "_";
		old_dir_name += std::to_string(timeinfo->tm_mday);
		old_dir_name += "-";
		old_dir_name += std::to_string(camData->camIndex);
		if(access(old_dir_name.c_str(), 0) == 0){  // 参数2为0时表示检查文件夹是否存在,存在返回0,不存在返回-1
			std::string rm_command = "rm -rf ";
			rm_command += old_dir_name;
			// rmdir(old_dir_name.c_str());
			system(rm_command.c_str());
		}

		// calc cur output filename
		out_filename = cur_dir_name; out_filename += "/";
		out_filename += std::to_string(timeinfo->tm_hour);	out_filename += "-";
		out_filename += std::to_string(timeinfo->tm_min);	out_filename += "-";
		out_filename += std::to_string(timeinfo->tm_sec);	out_filename += ".avi";
		const char *out_filename_char = out_filename.c_str();//Output file URL

		// create outFormatCtx
		avformat_alloc_output_context2(&ofmt_ctx, NULL, NULL, out_filename_char);
		if (!ofmt_ctx) {
			fprintf(stderr, "Could not create output context\n");
			ret = AVERROR_UNKNOWN;
		}

		ofmt = ofmt_ctx->oformat;

		// copy inFormatCtx to outFormatCtx
		for (int i = 0; i < ifmt_ctx->nb_streams; i++) {
			//根据输入流创建输出流（Create output AVStream according to input AVStream）
			AVStream *in_stream = ifmt_ctx->streams[i];
			AVCodecParameters *in_codecpar = in_stream->codecpar;
			AVStream *out_stream = avformat_new_stream(ofmt_ctx, in_stream->codec->codec);
			if (!out_stream) {
				printf( "Failed allocating output stream\n");
				ret = AVERROR_UNKNOWN;
			}

			ret = avcodec_parameters_copy(out_stream->codecpar, in_codecpar);
			if (ret < 0) {
				fprintf(stderr, "Failed to copy codec parameters\n");
			}

			out_stream->codecpar->codec_tag = 0;
		}
		av_dump_format(ofmt_ctx, 0, out_filename_char, 1);

		if (!(ofmt->flags & AVFMT_NOFILE)) {
			ret = avio_open(&ofmt_ctx->pb, out_filename_char, AVIO_FLAG_WRITE);
			if (ret < 0) {
				fprintf(stderr, "Could not open output file '%s'", out_filename_char);
			}
		}

		ret = avformat_write_header(ofmt_ctx, NULL);
		if (ret < 0) {
			fprintf(stderr, "Error occurred when opening output file\n");
		}

		for(;;){
			// std::cout << "============================================--------------------------- frame index: " << frame_index << std::endl;
			int timecmp = 0;
			AVStream *in_stream, *out_stream;

			//获取一个AVPacket（Get an AVPacket）
			do {
				ret = av_read_frame(ifmt_ctx, &pkt);                                // read frames
			} while (ret == AVERROR(EAGAIN));
			if (ret < 0) {
				printf("Could not read frame (error '%s')\n", av_make_error_string(errbuf, sizeof(errbuf), ret));
				break;
			}

			//FIX：No PTS (Example: Raw H.264)
			//Simple Write PTS
			if(pkt.pts==AV_NOPTS_VALUE){
				//Write PTS
				AVRational time_base1=ifmt_ctx->streams[videoindex]->time_base;
				//Duration between 2 frames (us)
				int64_t calc_duration=(double)AV_TIME_BASE/av_q2d(ifmt_ctx->streams[videoindex]->r_frame_rate);
				//Parameters
				pkt.pts=(double)(frame_index*calc_duration)/(double)(av_q2d(time_base1)*AV_TIME_BASE);
				pkt.dts=pkt.pts;
				pkt.duration=(double)calc_duration/(double)(av_q2d(time_base1)*AV_TIME_BASE);
			}
			//Important:Delay
			// if(pkt.stream_index == videoindex){
			// 	AVRational time_base=ifmt_ctx->streams[videoindex]->time_base;
			// 	AVRational time_base_q={1,AV_TIME_BASE};
			// 	int64_t pts_time = av_rescale_q(pkt.dts, time_base, time_base_q);
			// 	int64_t now_time = av_gettime() - start_time;
			// 	if (pts_time > now_time)
			// 		av_usleep(pts_time - now_time);

			// }

			in_stream  = ifmt_ctx->streams[pkt.stream_index];
			out_stream = ofmt_ctx->streams[pkt.stream_index];
			if (pkt.stream_index == videoindex) {
				/* copy packet */
				//转换PTS/DTS（Convert PTS/DTS）
				// pkt.pts = av_rescale_q_rnd(pkt.pts, in_stream->time_base, out_stream->time_base, (AVRounding)(AV_ROUND_NEAR_INF|AV_ROUND_PASS_MINMAX));
				// pkt.dts = av_rescale_q_rnd(pkt.dts, in_stream->time_base, out_stream->time_base, (AVRounding)(AV_ROUND_NEAR_INF|AV_ROUND_PASS_MINMAX));
				// AVStream->time_base.num  分子
				// AVStream->time_base.den  分母
				// num/den == 1/帧率
				out_stream->time_base.den = 40;  // 将帧率设置为40
				pkt.pts = frame_index * (out_stream->time_base.num * 1000 / out_stream->time_base.den);
				pkt.dts = pkt.pts;
				pkt.duration = av_rescale_q(pkt.duration, in_stream->time_base, out_stream->time_base);
				pkt.pos = -1;

				//ret = av_write_frame(ofmt_ctx, &pkt);
				ret = av_interleaved_write_frame(ofmt_ctx, &pkt);
				if (ret < 0) {
					printf( "Error muxing packet\n");
					break;
				}

				frame_index++;
			}

			av_free_packet(&pkt);

			time ( &rawtime );
			timeinfo = localtime ( &rawtime );
			if((timeinfo->tm_min <= single_video_long) && cur_begin_time_min > timeinfo->tm_min)
				timecmp = 60 - cur_begin_time_min + timeinfo->tm_min;
			else
				timecmp = timeinfo->tm_min - cur_begin_time_min;
			if((timecmp >= single_video_long) && (timeinfo->tm_min % 10 == 0))
				break;
		}
		av_write_trailer(ofmt_ctx);

		frame_index = 0;

		/* close output */
		if (ofmt_ctx && !(ofmt->flags & AVFMT_NOFILE))
			avio_closep(&ofmt_ctx->pb);
		avformat_free_context(ofmt_ctx);
	}
	avformat_close_input(&ifmt_ctx);
}

/************************************************************************
 **函数：push_stream
 **功能：推流视频
 ************************************************************************/
void* push_stream(void *arg)
{
	WebCamData *camData = (WebCamData *)arg;

	AVOutputFormat *ofmt = NULL;
	//输入对应一个AVFormatContext，输出对应一个AVFormatContext
	//（Input AVFormatContext and Output AVFormatContext）
	AVFormatContext *ifmt_ctx = NULL, *ofmt_ctx = NULL;
	AVPacket pkt;
	const char *in_filename, *out_filename;
	int ret;
	int videoindex = -1;
	int audioindex = -1;
	int frame_index=0;
	int64_t start_time=0;

	in_filename  = camData->webCamURI.c_str();//输入URL（Input file URL）

	out_filename = camData->pushStreamURL.c_str();//输出 URL（Output URL）[RTMP]

	av_register_all();
	//Network
	avformat_network_init();
	//输入（Input）
	if ((ret = avformat_open_input(&ifmt_ctx, in_filename, 0, 0)) < 0) {
		printf( "Could not open input file.");
	}
	if ((ret = avformat_find_stream_info(ifmt_ctx, 0)) < 0) {
		printf( "Failed to retrieve input stream information");
	}
	av_dump_format(ifmt_ctx, 0, in_filename, 0);

	for (int i = 0; i < ifmt_ctx->nb_streams; i++) {                                // find video stream index
		switch(ifmt_ctx->streams[i]->codec->codec_type) {
			case AVMEDIA_TYPE_AUDIO: audioindex = i; break;
			case AVMEDIA_TYPE_VIDEO: videoindex = i; break;
			default: break;
		}
	}
	if (-1 == videoindex) {
		printf("No H.264 video stream in the input file\n");
		return NULL;
	}

	while(1){
		//输出（Output）
		avformat_alloc_output_context2(&ofmt_ctx, NULL, "flv", out_filename); //RTMP
		//avformat_alloc_output_context2(&ofmt_ctx, NULL, "mpegts", out_filename);//UDP
		if (!ofmt_ctx) {
			printf( "Could not create output context\n");
			ret = AVERROR_UNKNOWN;
		}

		ofmt = ofmt_ctx->oformat;

		for (int i = 0; i < ifmt_ctx->nb_streams; i++) {
			//根据输入流创建输出流（Create output AVStream according to input AVStream）
			AVStream *in_stream = ifmt_ctx->streams[i];
			AVCodecParameters *in_codecpar = in_stream->codecpar;
			AVStream *out_stream = avformat_new_stream(ofmt_ctx, in_stream->codec->codec);
			if (!out_stream) {
				printf( "Failed allocating output stream\n");
				ret = AVERROR_UNKNOWN;
			}

			ret = avcodec_parameters_copy(out_stream->codecpar, in_codecpar);
			if (ret < 0) {
				fprintf(stderr, "Failed to copy codec parameters\n");
			}

			out_stream->codecpar->codec_tag = 0;
		}
		//Dump Format------------------
		av_dump_format(ofmt_ctx, 0, out_filename, 1);

		//打开输出URL（Open output URL）
		if (!(ofmt->flags & AVFMT_NOFILE)) {
			ret = avio_open(&ofmt_ctx->pb, out_filename, AVIO_FLAG_WRITE);
			if (ret < 0) {
				printf( "Could not open output URL '%s'", out_filename);
                continue;
			}
		}
		//写文件头（Write file header）
		ret = avformat_write_header(ofmt_ctx, NULL);
		if (ret < 0) {
			printf( "Error occurred when opening output URL\n");
		}

		start_time = av_gettime();
		while (1) {
			AVStream *in_stream, *out_stream;
			//获取一个AVPacket（Get an AVPacket）
			do {
				ret = av_read_frame(ifmt_ctx, &pkt);                                // read frames
			} while (ret == AVERROR(EAGAIN));
			// camData->getPacket(&pkt);
			if (ret < 0)
				break;
			//FIX：No PTS (Example: Raw H.264)
			//Simple Write PTS
			if(pkt.pts==AV_NOPTS_VALUE){
				//Write PTS
				AVRational time_base1=ifmt_ctx->streams[videoindex]->time_base;
				//Duration between 2 frames (us)
				int64_t calc_duration=(double)AV_TIME_BASE/av_q2d(ifmt_ctx->streams[videoindex]->r_frame_rate);
				//Parameters
				pkt.pts=(double)(frame_index*calc_duration)/(double)(av_q2d(time_base1)*AV_TIME_BASE);
				pkt.dts=pkt.pts;
				pkt.duration=(double)calc_duration/(double)(av_q2d(time_base1)*AV_TIME_BASE);
			}
			//Important:Delay
			// if(pkt.stream_index==videoindex){
			// 	AVRational time_base=ifmt_ctx->streams[videoindex]->time_base;
			// 	AVRational time_base_q={1,AV_TIME_BASE};
			// 	int64_t pts_time = av_rescale_q(pkt.dts, time_base, time_base_q);
			// 	int64_t now_time = av_gettime() - start_time;
			// 	if (pts_time > now_time)
			// 		av_usleep(pts_time - now_time);
			// }

			in_stream  = ifmt_ctx->streams[pkt.stream_index];
			out_stream = ofmt_ctx->streams[pkt.stream_index];
			/* copy packet */
			//转换PTS/DTS（Convert PTS/DTS）
			// pkt.pts = av_rescale_q_rnd(pkt.pts, in_stream->time_base, out_stream->time_base, (AVRounding)(AV_ROUND_NEAR_INF|AV_ROUND_PASS_MINMAX));
			// pkt.dts = av_rescale_q_rnd(pkt.dts, in_stream->time_base, out_stream->time_base, (AVRounding)(AV_ROUND_NEAR_INF|AV_ROUND_PASS_MINMAX));
			// pkt.duration = av_rescale_q(pkt.duration, in_stream->time_base, out_stream->time_base);
			// pkt.pos = -1;
			out_stream->time_base.den = 40;  // 将帧率设置为40
			pkt.pts = frame_index * (out_stream->time_base.num * 1000 / out_stream->time_base.den);
			pkt.dts = pkt.pts;
			pkt.duration = av_rescale_q(pkt.duration, in_stream->time_base, out_stream->time_base);
			pkt.pos = -1;

			//Print to Screen
			if(pkt.stream_index == videoindex){
				printf("Send %8d video frames to output %s\n", frame_index, out_filename);
				frame_index++;
			}
			//ret = av_write_frame(ofmt_ctx, &pkt);
			ret = av_interleaved_write_frame(ofmt_ctx, &pkt);

			if (ret < 0) {
				printf( "Error muxing packet\n");
				break;
			}

			av_free_packet(&pkt);
		}
		//写文件尾（Write file trailer）
		av_write_trailer(ofmt_ctx);

		/* close output */
		if (ofmt_ctx && !(ofmt->flags & AVFMT_NOFILE))
			avio_close(ofmt_ctx->pb);
		avformat_free_context(ofmt_ctx);
	}
	avformat_close_input(&ifmt_ctx);
}

void cb_discovery(char *DeviceXAddr)
{
	int stmno = 1;                                                              // 码流序号，0为主码流，1为辅码流
	int profile_cnt = 0;                                                        // 设备配置文件个数
	struct tagProfile *profiles = NULL;                                         // 设备配置文件列表
	struct tagCapabilities capa;                                                // 设备能力信息

	char uri[ONVIF_ADDRESS_SIZE] = {0};                                         // 不带认证信息的URI地址
	char uri_auth[ONVIF_ADDRESS_SIZE + 50] = {0};                               // 带有认证信息的URI地址

	//-- 校时
	ONVIF_GetSystemDateAndTime(DeviceXAddr);
    ONVIF_SetSystemDateAndTime(DeviceXAddr);
    ONVIF_GetSystemDateAndTime(DeviceXAddr);

	ONVIF_GetCapabilities(DeviceXAddr, &capa);                                  // 获取设备能力信息（获取媒体服务地址）

	profile_cnt = ONVIF_GetProfiles(capa.MediaXAddr, &profiles);                // 获取媒体配置信息（主/辅码流配置信息）

	if (profile_cnt > stmno) {
		ONVIF_GetStreamUri(capa.MediaXAddr, profiles[stmno].token, uri, sizeof(uri)); // 获取RTSP地址

		make_uri_withauth(uri, USERNAME, PASSWORD, uri_auth, sizeof(uri_auth)); // 生成带认证信息的URI（有的IPC要求认证）

		std::cout << "uri is: " << uri << std::endl;
		std::cout << "uri_auth is: " << uri_auth << std::endl;

		// open_rtsp(uri_auth);                                                    // 读取主码流的音视频数据
		camURIs.push_back(uri_auth);
	}

	if (NULL != profiles) {
		free(profiles);
		profiles = NULL;
	}
}

int init_videocap_module()
{
	ONVIF_DetectDevice(cb_discovery);

	return 0;
}

std::vector<std::string> getWebCamURI()
{
	return camURIs;
}
