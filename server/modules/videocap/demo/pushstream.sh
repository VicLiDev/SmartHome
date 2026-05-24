for((;;));
do
	ffmpeg -re -i /dev/video1 -vcodec h264 -f flv rtmp://192.168.1.5/live/livestream;
	sleep 1;
done
