#include "ofxFFmpeg.h"
// openFrameworks
#include "ofLog.h"
#include "ofSoundStream.h"
#include "ofVideoGrabber.h"

#include <string.h>


// Logging macros
#define LOG_ERROR() ofLogError( "ofxFFmpeg" ) << __FUNCTION__ << ": "
#define LOG_WARNING() ofLogWarning( "ofxFFmpeg" ) << __FUNCTION__ << ": "
#define LOG_NOTICE() ofLogNotice( "ofxFFmpeg" ) << __FUNCTION__ << ": "
#define LOG_VERBOSE() ofLogVerbose( "ofxFFmpeg" ) << __FUNCTION__ << ": "
#define LOG() LOG_NOTICE()

// Subprocess macros
#if defined( _WIN32 )
#define P_CLOSE( file ) _pclose( file )
#define P_OPEN( cmd ) _popen( cmd, "wb" )  // write binary?
#else
#define P_CLOSE( file ) pclose( file )
#define P_OPEN( cmd ) popen( cmd, "w" )
#endif

namespace ofxFFmpeg {

// -----------------------------------------------------------------
Recorder::Recorder()
{
}

// -----------------------------------------------------------------
Recorder::~Recorder()
{
	stop();
	if ( m_thread.joinable() ) m_thread.join();
}

// -----------------------------------------------------------------
bool Recorder::start( const RecorderSettings &settings, bool forceIfNotReady )
{

	if ( m_isRecording ) {
		LOG_WARNING() << "Can't start recording - already started.";
		return false;
	}

	if ( !isReady() ) {
		if ( forceIfNotReady ) {
			LOG_WARNING() << "Starting new recording - cancelling previous still-processing recording '" << m_settings.outputPath << "' and deleting " << numFramesInQueue() << " queued frames...";
			ofPixels *pixels = nullptr;
      
			while ( pixels = m_frames.pop().value_or(nullptr) ) {
        pixels->clear();
        delete pixels;
			}
		} else {
			LOG_ERROR() << "Can't start recording - previous recording is still processing " << numFramesInQueue() << " frames";
			return false;
		}
	}

	if ( settings.outputPath.empty() ) {
		LOG_ERROR() << "Can't start recording - output path is not set!";
		return false;
	}

	if ( ofFile::doesFileExist( ofToDataPath( m_settings.outputPath, true ), false ) && !m_settings.allowOverwrite ) {
		LOG_ERROR() << "The output file already exists and overwriting is disabled. Can't record to file: " << m_settings.outputPath;
		return false;
	}

	m_settings = settings;

	if ( m_settings.ffmpegPath.empty() ) {
		m_settings.ffmpegPath = "ffmpeg";
	}

	m_nAddedFrames = 0;

	std::string cmd               = m_settings.ffmpegPath;
	std::vector<std::string> args = {
	    "-y",   // overwrite
	    "-an",  // disable audio -- todo: add audio,
		m_settings.extraPreArgs,
	    // input
	    "-framerate " + ofToString( m_settings.fps ),                      // input frame rate
	    "-video_size " + std::to_string( m_settings.videoResolution.x ) +   // input resolution x
	        "x" + std::to_string( m_settings.videoResolution.y ),  // input resolution y
	    "-f rawvideo",                                             // input codec
		//"-pixel_format rgba",
	    "-pix_fmt rgba",                                          // input pixel format
	    m_settings.extraInputArgs,                                 // custom input args
	    "-i pipe:",                                                // input source (default pipe)
	    //"-vf 'format=nv12,hwupload'",
	    // output
	  	    
	};

	// add codec specific args if there is codec
	if (!m_settings.videoCodec.empty()) {
	  args.push_back("-r " + ofToString( m_settings.outFPS ));              // output frame rate
	  args.push_back("-c:v " + m_settings.videoCodec);                   // output codec
	  args.push_back("-b:v " + ofToString( m_settings.bitrate ) + "k");  // output bitrate kbps (hint)
	}

	args.push_back(ofToString(m_settings.extraOutputArgs));                        // custom output args
	args.push_back(ofToString(m_settings.outputPath));                              // output path


	for ( const auto &arg : args ) {
		if ( !arg.empty() ) cmd += " " + arg;
	}

	std::unique_lock<std::mutex> lck( m_pipeMtx );
	if ( m_ffmpegPipe ) {
		if ( P_CLOSE( m_ffmpegPipe ) < 0 ) {
			LOG_ERROR() << "Error closing FFmpeg pipe. Error: " << strerror(errno);
		}
	}
	lck.unlock();

	LOG() << "Starting recording with command...\n\t" << cmd << "\n";

	lck.lock();
	m_ffmpegPipe = P_OPEN( cmd.c_str() );
	if ( !m_ffmpegPipe ) {
		LOG_ERROR() << "Unable to open ffmpeg pipe to start recording. Error: " << strerror(errno);
		return false;
	}
	lck.unlock();

	LOG() << "ffmpeg pipe opened";

	m_isRecording = true;
	return true;
}

// -----------------------------------------------------------------
void Recorder::stop()
{
	m_isRecording = false;
}

// -----------------------------------------------------------------
bool Recorder::wantsFrame()
{
	if ( m_isRecording && m_ffmpegPipe ) {
		const float delta          = Seconds( Clock::now() - m_recordStartTime ).count() - getRecordedDuration();
		const size_t framesToWrite = delta * m_settings.fps;
		return framesToWrite > 0;
	}
	return false;
}

// -----------------------------------------------------------------
size_t Recorder::addFrame( const ofPixels &pixels )
{
	if ( !m_isRecording ) {
		LOG_ERROR() << "Can't add new frame - not in recording mode.";
		return 0;
	}

	if ( !m_ffmpegPipe ) {
		LOG_ERROR() << "Can't add new frame - FFmpeg pipe is invalid!";
		return 0;
	}

	if ( !pixels.isAllocated() ) {
		LOG_ERROR() << "Can't add new frame - input pixels not allocated!";
		return 0;
	}

  // restart if finished
	if ( m_nAddedFrames == 0 ) {
		if ( m_thread.joinable() ) m_thread.join();  //detach();
		m_thread          = std::thread( &Recorder::processFrame, this );
		m_recordStartTime = Clock::now();
		m_lastFrameTime   = m_recordStartTime;
	}

	// add new frame(s) at specified frame rate
	const float delta          = Seconds( Clock::now() - m_recordStartTime ).count() - getRecordedDuration();
	const size_t framesToWrite = delta * m_settings.fps;
	size_t written             = 0;
	ofPixels *pixPtr           = nullptr;

	// drop or duplicate frames to maintain constant framerate
	while ( m_nAddedFrames == 0 || framesToWrite > written ) {
	  // LOG_NOTICE() << "adding frame";
	  
		// if ( !pixPtr ) {
		// 	pixPtr = new ofPixels( pixels );  // copy pixel data
		// }

		pixPtr = new ofPixels( pixels );  // copy pixel data

		m_frames.push(pixPtr);
		

		// if ( written == framesToWrite - 1 ) {
		// 	// only the last frame we produce owns the pixel data
		// 	m_mtx.lock();
		// 	m_frames.produce( pixPtr );
		// 	m_mtx.unlock();
		// } else {
		// 	// otherwise, we reference the data
		// 	ofPixels *pixRef = new ofPixels();
		// 	pixRef->setFromExternalPixels( pixPtr->getData(), pixPtr->getWidth(), pixPtr->getHeight(), pixPtr->getPixelFormat() );  // re-use already copied pointer
		// 	m_mtx.lock();
		// 	m_frames.produce( pixRef );
		// 	m_mtx.unlock();
		// }

		++m_nAddedFrames;
		++written;
		m_lastFrameTime = Clock::now();

		// LOG_NOTICE() << "frame added";
	}

	return written;
}

// -----------------------------------------------------------------
void Recorder::processFrame()
{
	while ( m_isRecording ) {

		// TimePoint lastFrameTime = Clock::now();
		// const float framedur    = 1.f / m_settings.fps;

    ofPixels *pixels = nullptr;

		while ( m_frames.size() ) {  // allows finish processing queue after we call stop()

      if ( !m_isRecording ) {
        LOG_NOTICE() << "Recording stopped, but finishing frame queue - " << m_frames.size() << " remaining frames at " << m_settings.fps << " fps";
      }

      if ( (pixels = m_frames.pop().value_or(nullptr)) && pixels->isAllocated()) {
        LOG_NOTICE() << "sending frame to ffmpeg. queue rest size: " << m_frames.size();
        const unsigned char *data = pixels->getData();
				  //const size_t dataLength   = m_settings.videoResolution.x * m_settings.videoResolution.y * pixels->getBytesPerPixel();
        const size_t dataLength = pixels->getTotalBytes();
        
        LOG_NOTICE() << "writing " << dataLength << " bytes to ffmpeg";
        m_pipeMtx.lock();
        size_t written = m_ffmpegPipe ? fwrite( data, sizeof( char ), dataLength, m_ffmpegPipe ) : 0;
        fflush(m_ffmpegPipe);
        m_pipeMtx.unlock();
        
        if ( written <= 0 ) {
          LOG_WARNING() << "Unable to write the frame.";
				  }
        
        pixels->clear();
        delete pixels;
				        
        LOG_NOTICE() << "sent frame to ffmpeg. queue size: " << m_frames.size();
					
      }
      

      //
			// old version - timestamp based
			// 
			// // feed frames at constant fps
			// float delta = Seconds( Clock::now() - lastFrameTime ).count();

			// if ( delta >= framedur * 2) {

			// 	if ( !m_isRecording ) {
			// 		LOG_NOTICE() << "Recording stopped, but finishing frame queue - " << m_frames.size() << " remaining frames at " << m_settings.fps << " fps";
			// 	}

			// 	ofPixels *pixels = nullptr;
				
			// 	if ( (pixels = m_frames.pop().value_or(nullptr)) && pixels->isAllocated()) {
			// 	  LOG_NOTICE() << "sending frame to ffmpeg. queue rest size: " << m_frames.size();
			// 	  const unsigned char *data = pixels->getData();
			// 	  //const size_t dataLength   = m_settings.videoResolution.x * m_settings.videoResolution.y * pixels->getBytesPerPixel();
			// 	  const size_t dataLength = pixels->getTotalBytes();

			// 	  LOG_NOTICE() << "writing " << dataLength << " bytes to ffmpeg";
			// 	  m_pipeMtx.lock();
			// 	  size_t written = m_ffmpegPipe ? fwrite( data, sizeof( char ), dataLength, m_ffmpegPipe ) : 0;
			// 	  fflush(m_ffmpegPipe);
			// 	  m_pipeMtx.unlock();

			// 	  if ( written <= 0 ) {
			// 	    LOG_WARNING() << "Unable to write the frame.";
			// 	  }

			// 	  pixels->clear();
			// 	  delete pixels;
				  
			// 	  lastFrameTime = Clock::now();

			// 	  LOG_NOTICE() << "sent frame to ffmpeg. queue size: " << m_frames.size();
					
			// 	}
			// }
		}
	}

	LOG_NOTICE() << "Recording finished, closing ffmpeg pipe...";

	// close ffmpeg pipe once stopped recording

	m_pipeMtx.lock();
	if ( m_ffmpegPipe ) {
		if ( P_CLOSE( m_ffmpegPipe ) < 0 ) {
			LOG_ERROR() << "Error closing FFmpeg pipe. Error: " << strerror(errno);
		}
	}
	m_ffmpegPipe = nullptr;
	m_pipeMtx.unlock();

	LOG_NOTICE() << "ffmpeg pipe closed";

	m_nAddedFrames = 0;
}
}  // namespace ofxFFmpeg
