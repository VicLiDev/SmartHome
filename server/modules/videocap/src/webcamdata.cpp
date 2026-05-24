#include "webcamdata.h"


WebCamData::WebCamData(int index, std::string str): camIndex(index), webCamURI(str)
{
	m_mutex = PTHREAD_MUTEX_INITIALIZER;
}

WebCamData::~WebCamData(){}


int WebCamData::loadImg(cv::Mat &img)
{
    pthread_mutex_lock(&m_mutex);
    // std::cout << "------------------------------------------- load img lock" << std::endl;
    img.copyTo(webImg);
    // std::cout << "------------------------------------------- load img unlock" << std::endl;
    pthread_mutex_unlock(&m_mutex);
}

int WebCamData::getImg(cv::Mat &getImg)
{
    pthread_mutex_lock(&m_mutex);
    // std::cout << "------------------------------------- get img lock" << std::endl;
    webImg.copyTo(getImg);
    // std::cout << "------------------------------------- get img unlock" << std::endl;
    pthread_mutex_unlock(&m_mutex);
}