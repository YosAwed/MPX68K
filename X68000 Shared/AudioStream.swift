#if false

//import Foundation
import AVFoundation


class AudioStream {
    

    var samplingrate = 22050

    init (samplingrate: Int) {
		self.samplingrate = samplingrate
    }


    
    // エンジンの生成
    let audioEngine = AVAudioEngine()
    // ソースノードの生成
    let player = AVAudioPlayerNode()
    
    var buffer :AVAudioPCMBuffer?
    
    

    
    private var sourceNode : AVAudioSourceNode?
	func play(  )
    {
        debugLog("Audio Play", category: .audio)

        // プレイヤーノードからオーディオフォーマットを取得
             let outputNode = audioEngine.outputNode
        let format = outputNode.inputFormat(forBus: 0)
        debugLog("Audio sample rate: \(format.sampleRate)", category: .audio)
        debugLog("Audio format: \(format.commonFormat)", category: .audio)
		let audioFormat :AVAudioFormat = AVAudioFormat(commonFormat: .pcmFormatInt16, sampleRate: Double(self.samplingrate), channels: 2, interleaved: true )!// player.outputFormat(forBus: 0)
        let sampleRate = Float(audioFormat.sampleRate)
        debugLog("sampleRate: \(sampleRate)", category: .audio)
            
        
        sourceNode = AVAudioSourceNode(format: audioFormat , renderBlock: { (_, timeStamp, frameCount, audioBufferList) -> OSStatus in

                let ablPointer = UnsafeMutableAudioBufferListPointer(audioBufferList)
                let buf: UnsafeMutableBufferPointer<Int16> = UnsafeMutableBufferPointer(ablPointer[0])
                        debugLog("mNumberBuffers: \(audioBufferList.pointee.mNumberBuffers)", category: .audio)
                        debugLog("mDataByteSize: \(audioBufferList.pointee.mBuffers.mDataByteSize)", category: .audio)
                        debugLog("mNumberChannels: \(audioBufferList.pointee.mBuffers.mNumberChannels)", category: .audio)
        debugLog("frameCount: \(frameCount)", category: .audio)
                X68000_AudioCallBack(ablPointer[0].mData, UInt32(frameCount));

            return noErr
        })

        // オーディオエンジンにプレイヤーをアタッチ
        sourceNode?.reset()
        audioEngine.attach(sourceNode!)

        let mixer = audioEngine.mainMixerNode

        audioEngine.connect(sourceNode!, to: mixer, format: audioFormat)
        //        audioEngine.attach(player)
        // プレイヤーノードとミキサーノードを接続
  //      audioEngine.connect(player, to: mixer, format: audioFormat)
        // 再生の開始を設定
//        alloc()
        audioEngine.prepare()
        do {
          // エンジンを開始
          try audioEngine.start()
          // 再生
//          player.play()
        } catch let error {
          errorLog("Audio error", error: error, category: .audio)
        }



    }
    func stop()
    {
        debugLog("Audio Stop", category: .audio)

    }
    func pause()
    {
        debugLog("Audio Pause", category: .audio)

    }
    func close()
    {
        debugLog("Audio Close", category: .audio)
    }
}


 #else

import Foundation
import AudioToolbox

func bridge<T : AnyObject>(_ obj : T) -> UnsafeRawPointer {
    return UnsafeRawPointer(Unmanaged.passUnretained(obj).toOpaque())
    // return unsafeAddressOf(obj) // ***
}

func bridge<T : AnyObject>(_ ptr : UnsafeRawPointer) -> T {
    return Unmanaged<T>.fromOpaque(ptr).takeUnretainedValue()
    // return unsafeBitCast(ptr, T.self) // ***
}

func outputCallback(_ data: UnsafeMutableRawPointer?, queue: AudioQueueRef, buffer: AudioQueueBufferRef) {
    
    let audioData = buffer.pointee.mAudioData
    let size = buffer.pointee.mAudioDataBytesCapacity / 4  // Size in 16-bit stereo frames
    
    // Safety check for reasonable buffer size
    if size > 0 && size <= 8192 {
        let mAudioDataPtr = UnsafeMutablePointer<Int16>(OpaquePointer(audioData))
        
        // Let the X68000 audio core fill the buffer directly
        X68000_AudioCallBack(mAudioDataPtr, UInt32(size))
        
        buffer.pointee.mAudioDataByteSize = buffer.pointee.mAudioDataBytesCapacity
        AudioQueueEnqueueBuffer(queue, buffer, 0, nil)
    } else {
        // Fallback: clear and enqueue if size is invalid
        memset(audioData, 0, Int(buffer.pointee.mAudioDataBytesCapacity))
        buffer.pointee.mAudioDataByteSize = buffer.pointee.mAudioDataBytesCapacity
        AudioQueueEnqueueBuffer(queue, buffer, 0, nil)
    }
}


class AudioStream {
    var dataFormat:     AudioStreamBasicDescription
    var queue:          AudioQueueRef? = nil

    var buffers =       [AudioQueueBufferRef?](repeating: nil, count: 4)  // Increased buffer count for stability

    var bufferByteSize: UInt32
    
	var samplingrate = 22050
	
	init (samplingrate: Int) {
		self.samplingrate = samplingrate

        dataFormat = AudioStreamBasicDescription(
            mSampleRate:        Float64(samplingrate),
            mFormatID:          kAudioFormatLinearPCM,
            mFormatFlags:       kAudioFormatFlagIsSignedInteger | kAudioFormatFlagIsPacked,
            mBytesPerPacket:    4,
            mFramesPerPacket:   1,
            mBytesPerFrame:     4,
            mChannelsPerFrame:  2,
            mBitsPerChannel:    16,
            mReserved:          0
        )

        // Calculate buffer size based on sample rate to provide ~100ms of audio buffer for stability
        let bufferDurationSeconds: Float64 = 0.1  // 100ms for better stability and reduced stuttering
        let framesPerBuffer = UInt32(Float64(samplingrate) * bufferDurationSeconds)
        bufferByteSize = framesPerBuffer * dataFormat.mBytesPerFrame
        
        debugLog("Audio buffer: \(framesPerBuffer) frames, \(bufferByteSize) bytes", category: .audio)
//return;
        AudioQueueNewOutput(
            &dataFormat,
            outputCallback,
            unsafeBitCast(self, to: UnsafeMutableRawPointer.self),
            nil,  // Use internal thread for better performance
            nil,  // Use internal thread for better performance
            0,
            &queue)
        
        // Set audio queue properties for better performance and stability
        if let queue = queue {
            // Disable level metering for better performance
            var enableLevelMetering: UInt32 = 0
            AudioQueueSetProperty(queue, kAudioQueueProperty_EnableLevelMetering, &enableLevelMetering, UInt32(MemoryLayout<UInt32>.size))
        }
        
        load()
    
    }

    func load()
    {
        if let queue = self.queue {
            
            // Allocate and pre-fill all buffers
            for i in 0..<buffers.count {
                AudioQueueAllocateBuffer(queue, bufferByteSize, &buffers[i])
                if let buffer = buffers[i] {
                    // Pre-fill buffer with silence to ensure smooth startup
                    memset(buffer.pointee.mAudioData, 0, Int(bufferByteSize))
                    buffer.pointee.mAudioDataByteSize = bufferByteSize
                    outputCallback(unsafeBitCast(self, to: UnsafeMutableRawPointer.self), queue: queue, buffer: buffer)
                }
            }
            
            // Flush any previous state and prime the queue
            AudioQueueFlush(queue)
            let primeResult = AudioQueuePrime(queue, 0, nil)
            if primeResult != noErr {
                errorLog("Failed to prime audio queue: \(primeResult)", category: .audio)
            }
        }
    }

    func play()
    {
        debugLog("Audio Play", category: .audio)
        if let queue = self.queue {
            let result = AudioQueueStart(queue, nil)
            if result != noErr {
                errorLog("Failed to start audio queue: \(result)", category: .audio)
            }
        }
    }
    
    func stop()
    {
        debugLog("Audio Stop", category: .audio)
        if let queue = self.queue {
            let result = AudioQueueStop(queue, true)  // immediate stop
            if result != noErr {
                errorLog("Failed to stop audio queue: \(result)", category: .audio)
            }
        }
    }
    
    func pause()
    {
        debugLog("Audio Pause", category: .audio)
        if let queue = self.queue {
            let result = AudioQueuePause(queue)
            if result != noErr {
                errorLog("Failed to pause audio queue: \(result)", category: .audio)
            }
        }
    }
    func close()
    {
        debugLog("Audio Close", category: .audio)

        if let queue = self.queue {
            // Stop audio queue first
            AudioQueueStop(queue, true)
            
            // Flush any remaining buffers
            AudioQueueFlush(queue)
            
            // Free all buffers
            for buffer in buffers {
                if let buffer = buffer {
                    AudioQueueFreeBuffer(queue, buffer)
                }
            }
            
            // Dispose of the queue
            let result = AudioQueueDispose(queue, true)
            if result != noErr {
                errorLog("Failed to dispose audio queue: \(result)", category: .audio)
            }
            
            self.queue = nil
        }
    }
}

#endif
