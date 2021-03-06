/*
 *  VideoEncoderX264or5 - Base class for VideoEncoderX264 and VideoEncoderX265
 *  Copyright (C) 2013  Fundació i2CAT, Internet i Innovació digital a Catalunya
 *
 *  This file is part of media-streamer.
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 *  Authors:  Marc Palau <marc.palau@i2cat.net>
 *            David Cassany <david.cassany@i2cat.net>
 */

#include "VideoEncoderX264or5.hh"

VideoEncoderX264or5::VideoEncoderX264or5() :
OneToOneFilter(), inPixFmt(P_NONE), forceIntra(false), fps(0), bitrate(0), gop(0), 
    gopTime(0), refTime(std::chrono::microseconds(0)), threads(0), bFrames(-1),
    needsConfig(false), inPts(0), outPts(0), dts(0)
{
    fType = VIDEO_ENCODER;
    midFrame = av_frame_alloc();
    outputStreamInfo = new StreamInfo(VIDEO);
    outputStreamInfo->video.h264or5.annexb = true;
    initializeEventMap();
    configure0(DEFAULT_BITRATE, VIDEO_DEFAULT_FRAMERATE, DEFAULT_GOP, 
              DEFAULT_LOOKAHEAD, DEFAULT_B_FRAMES, DEFAULT_THREADS, DEFAULT_ANNEXB, DEFAULT_PRESET, 0);
}

VideoEncoderX264or5::~VideoEncoderX264or5()
{
    if (midFrame){
        av_frame_free(&midFrame);
    }
}

bool VideoEncoderX264or5::doProcessFrame(Frame *org, Frame *dst)
{
    FrameTimeParams frameTP;
    
    if (!(org && dst)) {
        utils::errorMsg("Error encoding video frame: org or dst are NULL");
        return false;
    }

    VideoFrame* rawFrame = dynamic_cast<VideoFrame*> (org);
    VideoFrame* codedFrame = dynamic_cast<VideoFrame*> (dst);

    if (!rawFrame || !codedFrame) {
        utils::errorMsg("Error encoding video frame: org and dst MUST be VideoFrame");
        return false;
    }

    //TODO: recofigure with estimated fps
    if (!reconfigure(rawFrame, codedFrame)) {
        utils::errorMsg("Error encoding video frame: reconfigure failed");
        return false;
    }

    if (!fill_x264or5_picture(rawFrame)){
        utils::errorMsg("Could not fill x264_picture_t from frame");
        return false;
    }
    
    if (gopTime > 0) { 
        if (refTime.count() == 0){
            refTime = org->getPresentationTime();
        }
        
        std::chrono::microseconds diff = 
            std::chrono::duration_cast<std::chrono::microseconds>(refTime - org->getPresentationTime());
            
        if (diff.count() <= 0){
            forceIntra = true;
            refTime += std::chrono::microseconds(gopTime);
        }
    }
    
    frameTP.pTime = org->getPresentationTime();
    frameTP.oTime = org->getOriginTime();
    frameTP.seqNum = org->getSequenceNumber();
    qFTP[inPts] = frameTP;
    
    if (!encodeFrame(codedFrame)) {
        utils::warningMsg("Could not encode video frame");
        return false;
    }

    codedFrame->setSize(rawFrame->getWidth(), rawFrame->getHeight());
    
    dst->setConsumed(true);
    dst->setPresentationTime(qFTP[outPts].pTime);
    dst->setDecodeTime(qFTP[dts].pTime);
    dst->setOriginTime(qFTP[outPts].oTime);
    dst->setSequenceNumber(qFTP[outPts].seqNum);
    
    qFTP.erase(dts);
    
    return true;
}

//TODO: this should be done without libav
bool VideoEncoderX264or5::fill_x264or5_picture(VideoFrame* videoFrame)
{
    if (av_image_fill_arrays(midFrame->data, midFrame->linesize, videoFrame->getDataBuf(),
            (AVPixelFormat) libavInPixFmt, videoFrame->getWidth(),
            videoFrame->getHeight(), 1) <= 0){
        utils::errorMsg("Could not feed AVFrame");
        return false;
    }

    if (!fillPicturePlanes(midFrame->data, midFrame->linesize)) {
        utils::errorMsg("Could not fill picture planes");
        return false;
    }

    return true;
}

bool VideoEncoderX264or5::configure0(unsigned bitrate_, unsigned fps_, unsigned gop_, 
                                     unsigned lookahead_, int bFrames_, unsigned threads_, 
                                     bool annexB_, std::string preset_, unsigned gopTime_)
{
    if (bitrate_ <= 0 || gop_ <= 0 || lookahead_ < 0 || threads_ <= 0 || preset_.empty()) {
        utils::errorMsg("Error configuring VideoEncoderX264or5: invalid configuration values");
        return false;
    }

    bitrate = bitrate_;
    gop = gop_;
    lookahead = lookahead_;
    threads = threads_;
    bFrames = bFrames_;

    outputStreamInfo->video.h264or5.annexb = annexB_;
    preset = preset_;

    if (fps_ <= 0) {
        fps = VIDEO_DEFAULT_FRAMERATE;
        setFrameTime(std::chrono::microseconds(0));
    } else {
        fps = fps_;
        setFrameTime(std::chrono::microseconds(std::micro::den/fps));
    }
    
    if (gopTime_ > 0){
        if (gopTime_ < MIN_GOP_TIME){
            gopTime = MIN_GOP_TIME;
        } else {
            gopTime = gopTime_;
        }
    } else {
        gopTime = 0; 
    }

    needsConfig = true;
    return true;
}

bool VideoEncoderX264or5::configEvent(Jzon::Node* params)
{
    unsigned tmpBitrate;
    unsigned tmpFps;
    unsigned tmpGop;
    unsigned tmpGopTime;
    unsigned tmpLookahead;
    unsigned tmpThreads;
    unsigned tmpBFrames;
    bool tmpAnnexB;
    std::string tmpPreset;

    if (!params) {
        return false;
    }

    tmpBitrate = bitrate;
    tmpFps = fps;
    tmpGop = gop;
    tmpGopTime = gopTime;
    tmpLookahead = lookahead;
    tmpThreads = threads;
    tmpAnnexB = outputStreamInfo->video.h264or5.annexb;
    tmpPreset = preset;
    tmpBFrames = bFrames;

    if (params->Has("bitrate") && params->Get("bitrate").IsNumber()) {
        tmpBitrate = params->Get("bitrate").ToInt();
    }

    if (params->Has("fps") && params->Get("fps").IsNumber()) {
        tmpFps = params->Get("fps").ToInt();
    }

    if (params->Has("gop") && params->Get("gop").IsNumber()) {
        tmpGop = params->Get("gop").ToInt();
    }

    if (params->Has("lookahead") && params->Get("lookahead").IsNumber()) {
        tmpLookahead = params->Get("lookahead").ToInt();
    }
    
    if (params->Has("bframes") && params->Get("bframes").IsNumber()) {
        tmpBFrames = params->Get("bframes").ToInt();
    }

    if (params->Has("threads") && params->Get("threads").IsNumber()) {
        tmpThreads = params->Get("threads").ToInt();
    }

    if (params->Has("annexb") && params->Get("annexb").IsBool()) {
        tmpAnnexB = params->Get("annexb").ToBool();
    }

    if (params->Has("preset")) {
        tmpPreset = params->Get("preset").ToString();
    }
    
    if (params->Has("gopTime") && params->Get("gopTime").IsNumber()) {
        tmpGopTime = params->Get("gopTime").ToInt();
    }

    return configure0(tmpBitrate, tmpFps, tmpGop, tmpLookahead, tmpBFrames,
                      tmpThreads, tmpAnnexB, tmpPreset, tmpGopTime);
}

bool VideoEncoderX264or5::forceIntraEvent(Jzon::Node*)
{
    forceIntra = true;
    return true;
}

bool VideoEncoderX264or5::setGopReferenceTimeEvent(Jzon::Node* params)
{
    if (params->Has("referenceTime") && params->Get("referenceTime").IsString()) {
        std::string refTime_str = params->Get("referenceTime").ToString();
        refTime = std::chrono::microseconds(std::stoull(refTime_str));
        return true;
    }
    return false;
}

void VideoEncoderX264or5::initializeEventMap()
{
    eventMap["forceIntra"] = std::bind(&VideoEncoderX264or5::forceIntraEvent, this, std::placeholders::_1);
    eventMap["gopReferenceTime"] = std::bind(&VideoEncoderX264or5::setGopReferenceTimeEvent, this, std::placeholders::_1);
    eventMap["configure"] = std::bind(&VideoEncoderX264or5::configEvent, this, std::placeholders::_1);
}

void VideoEncoderX264or5::doGetState(Jzon::Object &filterNode)
{
    filterNode.Add("bitrate", (int) bitrate);
    filterNode.Add("fps", (int) fps);
    filterNode.Add("gop", (int) gop);
    filterNode.Add("gopTime", (int) gopTime);
    //Jzon has no long types, only int, so using string
    filterNode.Add("refTime", std::to_string(refTime.count()));
    filterNode.Add("lookahead", (int) lookahead);
    filterNode.Add("threads", (int) threads);
    filterNode.Add("annexb", outputStreamInfo->video.h264or5.annexb);
    filterNode.Add("bframes", (int) bFrames);
    filterNode.Add("preset", preset);
}

bool VideoEncoderX264or5::configure(int bitrate, int fps, int gop,
                                    int lookahead, int bFrames, int threads,
                                    bool annexB, std::string preset, int gopTime)
{
    Jzon::Object root, params;
    root.Add("action", "configure");
    params.Add("bitrate", bitrate);
    params.Add("fps", fps);
    params.Add("gop", gop);
    params.Add("gopTime", gopTime);
    params.Add("lookahead", lookahead);
    params.Add("bframes", bFrames);
    params.Add("threads", threads);
    params.Add("annexb", annexB);
    params.Add("preset", preset);
    root.Add("params", params);

    Event e(root, std::chrono::system_clock::now(), 0);
    pushEvent(e); 
    return true;
}

