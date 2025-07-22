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
        print("Play")

        // プレイヤーノードからオーディオフォーマットを取得
             let outputNode = audioEngine.outputNode
        let format = outputNode.inputFormat(forBus: 0)
        print("\(format.sampleRate)")
        print("\(format.commonFormat)")
		let audioFormat :AVAudioFormat = AVAudioFormat(commonFormat: .pcmFormatInt16, sampleRate: Double(self.samplingrate), channels: 2, interleaved: true )!// player.outputFormat(forBus: 0)
        let sampleRate = Float(audioFormat.sampleRate)
        print("sampleRate:\(sampleRate)")
            
        
        sourceNode = AVAudioSourceNode(format: audioFormat , renderBlock: { (_, timeStamp, frameCount, audioBufferList) -> OSStatus in

                let ablPointer = UnsafeMutableAudioBufferListPointer(audioBufferList)
                let buf: UnsafeMutableBufferPointer<Int16> = UnsafeMutableBufferPointer(ablPointer[0])
                        print("mNumberBuffers: \(audioBufferList.pointee.mNumberBuffers)")
                        print("mDataByteSize: \(audioBufferList.pointee.mBuffers.mDataByteSize)")
                        print("mNumberChannels: \(audioBufferList.pointee.mBuffers.mNumberChannels)")
        print(frameCount)
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
          print(error)
        }



    }
    func stop()
    {
        print("Stop")

    }
    func pause()
    {
        print("Pause")

    }
    func close()
    {
        print("Close")
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
    let size = buffer.pointee.mAudioDataBytesCapacity / 4
    
    // Enhanced safety check with better noise prevention
    if size > 0 && size <= 16384 {  // Increased buffer size limit for better compatibility
        let mAudioDataPrt = UnsafeMutablePointer<Int16>(OpaquePointer(audioData))
        
        // Pre-clear buffer to prevent noise artifacts from previous data
        memset(audioData, 0, Int(buffer.pointee.mAudioDataBytesCapacity))
        
        // Call the audio generation function
        X68000_AudioCallBack(mAudioDataPrt, UInt32(size))
        
        buffer.pointee.mAudioDataByteSize = buffer.pointee.mAudioDataBytesCapacity
        AudioQueueEnqueueBuffer(queue, buffer, 0, nil)
    } else {
        // Clear and enqueue if size is invalid
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

        // Calculate buffer size based on sample rate to provide ~50ms of audio buffer for lower latency
        let bufferDurationSeconds: Float64 = 0.05  // 50ms for reduced latency and stuttering
        let framesPerBuffer = UInt32(Float64(samplingrate) * bufferDurationSeconds)
        bufferByteSize = framesPerBuffer * dataFormat.mBytesPerFrame
        
        print("Audio buffer: \(framesPerBuffer) frames, \(bufferByteSize) bytes")
//return;
        AudioQueueNewOutput(
            &dataFormat,
            outputCallback,
            unsafeBitCast(self, to: UnsafeMutableRawPointer.self),
            nil,  // Use internal thread for better performance
            nil,  // Use internal thread for better performance
            0,
            &queue)
        
        // Set audio queue properties for better performance
        if let queue = queue {
            // Set high priority for audio processing
            var priority: UInt32 = 1  // High priority
            AudioQueueSetProperty(queue, kAudioQueueProperty_TimePitchBypass, &priority, UInt32(MemoryLayout<UInt32>.size))
            
            // Enable hardware acceleration if available
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
                print("Error: Failed to prime audio queue: \(primeResult)")
            }
        }
    }

    func play()
    {
        print("Audio Play")
        if let queue = self.queue {
            let result = AudioQueueStart(queue, nil)
            if result != noErr {
                print("Error: Failed to start audio queue: \(result)")
            }
        }
    }
    
    func stop()
    {
        print("Audio Stop")
        if let queue = self.queue {
            let result = AudioQueueStop(queue, true)  // immediate stop
            if result != noErr {
                print("Error: Failed to stop audio queue: \(result)")
            }
        }
    }
    
    func pause()
    {
        print("Audio Pause")
        if let queue = self.queue {
            let result = AudioQueuePause(queue)
            if result != noErr {
                print("Error: Failed to pause audio queue: \(result)")
            }
        }
    }
    func close()
    {
        print("Audio Close")

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
                print("Error: Failed to dispose audio queue: \(result)")
            }
            
            self.queue = nil
        }
    }
}

#endif
