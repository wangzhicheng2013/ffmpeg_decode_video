#include <iostream>
#include "ffmpeg_decode_video.hpp"

int main() {
	auto ffmpeg_ptr = std::make_shared<ffmpeg_decode_video>();
	if (false == ffmpeg_ptr->init()) {
		return -1;
	}
	if (false == ffmpeg_ptr->decode("/usr/tmp/shutdown_eye.mp4")) {
		return -2;
	}
	if (true == ffmpeg_ptr->output_pictures(nullptr, "/usr/tmp1")) {
		LOG_I("output pictures over!");
	}
	return 0;
}
