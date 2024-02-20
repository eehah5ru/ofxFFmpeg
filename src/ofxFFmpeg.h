#pragma once
#include "ofxFFmpegHelpers.h"

namespace ofxFFmpeg {

struct RecorderSettings
{
	std::string outputPath      = "output.mp4";
	glm::ivec2 videoResolution  = { 640, 480 };
	float fps                   = 30.f;
	float outFPS				= 30.f;
	unsigned int bitrate        = 20000;  // kbps
	std::string videoCodec      = "h264_vaapi"; // libx264
	std::string extraPreArgs    = "-hwaccel vaapi -vaapi_device /dev/dri/renderD128";
	std::string extraInputArgs  = "";
	//std::string extraOutputArgs = "-vf 'format=nv12,hwupload'";  // -pix_fmt yuva420p -g 1 -crf 0 -preset ultrafast -tune zerolatency setpts='(RTCTIME - RTCSTART) / (TB * 1000000)'
	std::string extraOutputArgs = "";
	bool allowOverwrite         = true;
	std::string ffmpegPath      = "ffmpeg";
};

class Recorder
{
public:
	Recorder();
	~Recorder();

	bool start( const RecorderSettings& settings, bool forceIfNotReady = false );
	void stop();

	bool wantsFrame();                          // returns true if recorder is ready for new frame
	size_t addFrame( const ofPixels& pixels );  // returns the number of frames added to queue

	bool isRecording() const { return m_isRecording.load(); }
	bool isReady() const { return m_isRecording.load() == false && m_frames.size() == 0; }
	float getRecordedDuration() const { return m_nAddedFrames / m_settings.fps; }
	size_t numFramesInQueue() { return m_frames.size(); }


	const RecorderSettings& getSettings() const { return m_settings; }

protected:
	RecorderSettings m_settings;
	std::atomic<bool> m_isRecording;
	FILE* m_ffmpegPipe = nullptr;
	TimePoint m_recordStartTime, m_lastFrameTime;
	unsigned int m_nAddedFrames;
	std::thread m_thread;
	LockFreeQueue<ofPixels*> m_frames;
	std::mutex m_mtx, m_pipeMtx;

	void processFrame();
};

}  // namespace ofxFFmpeg