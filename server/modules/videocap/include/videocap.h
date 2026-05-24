#ifndef VIDEOCAP_H
#define VIDEOCAP_H

#include <iostream>
#include <vector>
#include <string.h>

void* open_rtsp(void *arg);
void* save_to_video(void *arg);
void* push_stream(void *arg);
int init_videocap_module();
std::vector<std::string> getWebCamURI();

#endif // VIDEOCAP_H