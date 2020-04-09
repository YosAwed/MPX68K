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

//    let stream: AudioStream = bridge(UnsafeRawPointer(data)!)
    
    let size = buffer.pointee.mAudioDataBytesCapacity / 4
    let opaquePtr = OpaquePointer(buffer.pointee.mAudioData)
    let mAudioDataPrt = UnsafeMutablePointer<Int16>(opaquePtr)
    X68000_AudioCallBack(mAudioDataPrt, UInt32(size));

    buffer.pointee.mAudioDataByteSize = buffer.pointee.mAudioDataBytesCapacity
    AudioQueueEnqueueBuffer(queue, buffer, 0, nil)

}


class AudioStream {
    var dataFormat:     AudioStreamBasicDescription
    var queue:          AudioQueueRef? = nil

    var buffers =       [AudioQueueBufferRef?](repeating: nil, count: 2)

    var bufferByteSize: UInt32
    
	var samplingrate = 22050
	
	init (samplingrate: Int) {
		self.samplingrate = samplingrate

        dataFormat = AudioStreamBasicDescription(
            mSampleRate:        Float64(samplingrate),
            mFormatID:          kAudioFormatLinearPCM,
            mFormatFlags:       kAudioFormatFlagIsSignedInteger | kAudioFormatFlagIsPacked, // Bigendian??
            mBytesPerPacket:    4,
            mFramesPerPacket:   1,
            mBytesPerFrame:     4,
            mChannelsPerFrame:  2,
            mBitsPerChannel:    16,
            mReserved:          0
        )

        bufferByteSize   = 1024 * dataFormat.mBytesPerFrame
//return;
        AudioQueueNewOutput(
            &dataFormat,
            outputCallback,
            unsafeBitCast(self, to: UnsafeMutableRawPointer.self),
            CFRunLoopGetCurrent(),
            CFRunLoopMode.commonModes.rawValue,
            0,
            &queue)
        
        load()
    
    }

    func load()
    {
        if let queue = self.queue {
            
            for case var buffer in buffers {
                AudioQueueAllocateBuffer(queue, bufferByteSize, &buffer)
                outputCallback(unsafeBitCast(self, to: UnsafeMutableRawPointer.self), queue: queue, buffer: buffer!)
            }
            AudioQueueFlush(queue)
            AudioQueuePrime(queue,0,nil)
        }
    }

    func play()
    {
        print("Play")
        if let queue = self.queue {
            AudioQueueStart(queue, nil)
        }
    }
    func stop()
    {
        print("Stop")
        if let queue = self.queue {
            AudioQueueStop(queue, true)
        }

    }
    func pause()
    {
        print("Pause")
        if let queue = self.queue {
            AudioQueuePause(queue)
        }

    }
    func close()
    {
        print("Close")

        if let queue = self.queue {
            AudioQueueFlush(queue)
            if let buffer = buffers[0] { AudioQueueFreeBuffer(queue, buffer) } // <- V not necessary/
            if let buffer = buffers[1] { AudioQueueFreeBuffer(queue, buffer) }
            AudioQueueDispose(queue, true)
            self.queue = nil
        }
        

    }
}

#endif
