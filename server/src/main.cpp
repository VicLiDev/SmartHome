/************************************************************************
 **
 ** 作者：许振坪
 ** 日期：2017-05-03
 ** 博客：http://blog.csdn.net/benkaoya
 ** 描述：读取IPC音视频流数据示例代码
 **
 ************************************************************************/
#include <iostream>
#include <vector>
#include <string>

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

#include "thread_pool.h"
}

#include <opencv2/opencv.hpp>

#include "onvif_comm.h"
#include "onvif_dump.h"

#include "webcamdata.h"
#include "videocap.h"

int main(int argc, char **argv)
{
	//========================== pullImg
	std::vector<std::string> camURIs;
	std::vector<WebCamData> m_webCamDataV;

	init_videocap_module();
	camURIs = getWebCamURI();
	for(int i = 0; i < camURIs.size(); i++){
		std::string pushStreamURLTmp = "rtmp://192.168.1.5/live/livestream/cam" + std::to_string(i);
		WebCamData camDataTmp = WebCamData(i, camURIs.at(i));
		camDataTmp.pushStreamURL = pushStreamURLTmp;
		m_webCamDataV.push_back(camDataTmp);
	}

	pool_init (6);/*线程池中最多三个活动线程*/
	for(int i = 0; i < m_webCamDataV.size(); i++){
		pool_add_worker(open_rtsp, &m_webCamDataV[i]);
		pool_add_worker(save_to_video, &m_webCamDataV[i]);
		pool_add_worker(push_stream, &m_webCamDataV[i]);
	}
	//========================== pullImg end

	std::vector<cv::Mat> views(m_webCamDataV.size());
	for(int i = 0;;i++){
		for(int i = 0; i < m_webCamDataV.size(); i++){
			m_webCamDataV[i].getImg(views[i]);
		}

		for(int i = 0; i < views.size(); i++){
			if(views[i].empty()){
				std::cout << "img " << std::to_string(i) << " is empty!!" << std::endl;
				continue;
			}else{
				// 工作时间长了以后画面显示会卡住，但是通过 imwrite 可以看到，获取图像的线程是在正常工作的
				cv::imshow("getImg" + std::to_string(i), views[i]);
				// cv::imwrite("getImg" + std::to_string(i) + ".jpg", views[i]);
			}
		}
		// 延时不能太短，不然加载图像的线程无法获取互斥锁，如果这里有耗时比较久的算法可以减少延时时长
		char key = (char)cv::waitKey(1);
		if( key == 27 )
			break;

		//===================================== 算法
		// ...

		//===================================== 算法 end
	}

	//释放线程
	pool_destroy ();

	return 0;
}
