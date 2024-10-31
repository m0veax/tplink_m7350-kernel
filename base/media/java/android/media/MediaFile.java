/*
 * Copyright (C) 2007 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

package android.media;

import android.content.ContentValues;
import android.provider.MediaStore.Audio;
import android.provider.MediaStore.Images;
import android.provider.MediaStore.Video;
import android.media.DecoderCapabilities;
import android.media.DecoderCapabilities.VideoDecoder;
import android.media.DecoderCapabilities.AudioDecoder;
import android.os.SystemProperties;
import java.io.FileReader;
import java.lang.String;
import android.util.Log;
import java.util.HashMap;
import java.util.Iterator;
import java.util.List;

/**
 * MediaScanner helper class.
 *
 * {@hide}
 */
public class MediaFile {
    // comma separated list of all file extensions supported by the media scanner
    public final static String sFileExtensions;

    // Audio file types
    public static final int FILE_TYPE_MP3     = 1;
    public static final int FILE_TYPE_M4A     = 2;
    public static final int FILE_TYPE_WAV     = 3;
    public static final int FILE_TYPE_AMR     = 4;
    public static final int FILE_TYPE_AWB     = 5;
    public static final int FILE_TYPE_WMA     = 6;
    public static final int FILE_TYPE_OGG     = 7;
    public static final int FILE_TYPE_AAC     = 8;
    public static final int FILE_TYPE_MKA     = 9;
    public static final int FILE_TYPE_3GPA    = 10;
    public static final int FILE_TYPE_AC3     = 11;
    public static final int FILE_TYPE_QCP     = 12;
    public static final int FILE_TYPE_PCM     = 13;
    public static final int FILE_TYPE_WEBMA   = 14;
    private static final int FIRST_AUDIO_FILE_TYPE = FILE_TYPE_MP3;
    private static final int LAST_AUDIO_FILE_TYPE = FILE_TYPE_WEBMA;

    // MIDI file types
    public static final int FILE_TYPE_MID     = 15;
    public static final int FILE_TYPE_SMF     = 16;
    public static final int FILE_TYPE_IMY     = 17;
    private static final int FIRST_MIDI_FILE_TYPE = FILE_TYPE_MID;
    private static final int LAST_MIDI_FILE_TYPE = FILE_TYPE_IMY;
   
    // Video file types
    public static final int FILE_TYPE_MP4     = 21;
    public static final int FILE_TYPE_M4V     = 22;
    public static final int FILE_TYPE_3GPP    = 23;
    public static final int FILE_TYPE_3GPP2   = 24;
    public static final int FILE_TYPE_WMV     = 25;
    public static final int FILE_TYPE_ASF     = 26;
    public static final int FILE_TYPE_MKV     = 27;
    public static final int FILE_TYPE_MP2TS   = 28;
    public static final int FILE_TYPE_AVI     = 29;
    public static final int FILE_TYPE_DIVX    = 30;
    public static final int FILE_TYPE_WEBM    = 31;
    private static final int FIRST_VIDEO_FILE_TYPE = FILE_TYPE_MP4;
    private static final int LAST_VIDEO_FILE_TYPE = FILE_TYPE_WEBM;
    
    // Image file types
    public static final int FILE_TYPE_JPEG    = 32;
    public static final int FILE_TYPE_GIF     = 33;
    public static final int FILE_TYPE_PNG     = 34;
    public static final int FILE_TYPE_BMP     = 35;
    public static final int FILE_TYPE_WBMP    = 36;
    private static final int FIRST_IMAGE_FILE_TYPE = FILE_TYPE_JPEG;
    private static final int LAST_IMAGE_FILE_TYPE = FILE_TYPE_WBMP;
   
    // Playlist file types
    public static final int FILE_TYPE_M3U     = 41;
    public static final int FILE_TYPE_PLS     = 42;
    public static final int FILE_TYPE_WPL     = 43;
    private static final int FIRST_PLAYLIST_FILE_TYPE = FILE_TYPE_M3U;
    private static final int LAST_PLAYLIST_FILE_TYPE = FILE_TYPE_WPL;
    
    private static FileReader socinfo_fd;
    private static char[] socinfo = new char[20];;
    private static String build_id;
    private static int error;
    static class MediaFileType {
    
        int fileType;
        String mimeType;
        
        MediaFileType(int fileType, String mimeType) {
            this.fileType = fileType;
            this.mimeType = mimeType;
        }
    }
    
    private static HashMap<String, MediaFileType> sFileTypeMap 
            = new HashMap<String, MediaFileType>();
    private static HashMap<String, Integer> sMimeTypeMap 
            = new HashMap<String, Integer>();            
    static void addFileType(String extension, int fileType, String mimeType) {
        sFileTypeMap.put(extension, new MediaFileType(fileType, mimeType));
        sMimeTypeMap.put(mimeType, Integer.valueOf(fileType));
    }

    private static boolean isWMAEnabled() {
        List<AudioDecoder> decoders = DecoderCapabilities.getAudioDecoders();
        try {
            socinfo_fd = new FileReader("/sys/devices/system/soc/soc0/build_id");
            error = socinfo_fd.read(socinfo,0,20);
            if (error==-1)
                Log.e("MediaFile","error in reading from build_id file");
            socinfo_fd.close();
        } catch(Exception e) {
            Log.e("MediaFile","Exception in FileReader");
        }
        build_id = new String(socinfo,17,1);
        for (AudioDecoder decoder: decoders) {
            if (decoder == AudioDecoder.AUDIO_DECODER_WMA) {
                if ("msm7630_surf".equals(SystemProperties.get("ro.board.platform")))
                {
                    if (build_id.equals("0")) {
                        return false;
                    }
                    else
                    {
                        return true;
                    }
                }
                else
                {
                    return true;
                }
            }
        }
        return false;
    }

    private static boolean isWMVEnabled() {
        List<VideoDecoder> decoders = DecoderCapabilities.getVideoDecoders();
        try {
            socinfo_fd = new FileReader("/sys/devices/system/soc/soc0/build_id");
            error = socinfo_fd.read(socinfo,0,20);
            if(error==-1)
                Log.e("MediaFile","error in reading from build_id file");
            socinfo_fd.close();
        } catch(Exception e) {
            Log.e("MediaFile","Exception in FileReader");
        }
        build_id = new String(socinfo,17,1);
        for (VideoDecoder decoder: decoders) {
            if (decoder == VideoDecoder.VIDEO_DECODER_WMV) {
                if ("msm7630_surf".equals(SystemProperties.get("ro.board.platform")))
                {
                    if (build_id.equals("0")) {
                        return false;
                    }
                    else
                    {
                        return true;
                    }
                }
                else
                {
                    return true;
                }
            }
        }
        return false;
    }

    static {
        addFileType("MP3", FILE_TYPE_MP3, "audio/mpeg");
        addFileType("M4A", FILE_TYPE_M4A, "audio/mp4");
        addFileType("WAV", FILE_TYPE_WAV, "audio/x-wav");
        addFileType("WAV", FILE_TYPE_PCM, "audio/wav");
        addFileType("AMR", FILE_TYPE_AMR, "audio/amr");
        addFileType("AWB", FILE_TYPE_AWB, "audio/amr-wb");
        if (isWMAEnabled()) {
            addFileType("WMA", FILE_TYPE_WMA, "audio/x-ms-wma");
        }
        addFileType("QCP", FILE_TYPE_QCP, "audio/qcp");
        addFileType("OGG", FILE_TYPE_OGG, "application/ogg");
        addFileType("OGA", FILE_TYPE_OGG, "audio/ogg");
        addFileType("WEBM", FILE_TYPE_WEBMA, "audio/webm");
        addFileType("AAC", FILE_TYPE_AAC, "audio/aac");
        addFileType("MKA", FILE_TYPE_MKA, "audio/x-matroska");
        addFileType("3GPP", FILE_TYPE_3GPA, "audio/3gpp");
        addFileType("MID", FILE_TYPE_MID, "audio/midi");
        addFileType("MIDI", FILE_TYPE_MID, "audio/midi");
        addFileType("XMF", FILE_TYPE_MID, "audio/midi");
        addFileType("MXMF", FILE_TYPE_MID, "audio/mobile-xmf");
        addFileType("RTTTL", FILE_TYPE_MID, "audio/midi");
        addFileType("SMF", FILE_TYPE_SMF, "audio/sp-midi");
        addFileType("IMY", FILE_TYPE_IMY, "audio/imelody");
        addFileType("RTX", FILE_TYPE_MID, "audio/midi");
        addFileType("OTA", FILE_TYPE_MID, "audio/midi");
        
        addFileType("MPEG", FILE_TYPE_MP4, "video/mpeg");
        addFileType("MP4", FILE_TYPE_MP4, "video/mp4");
        addFileType("M4V", FILE_TYPE_M4V, "video/mp4");
        addFileType("3GP", FILE_TYPE_3GPP, "video/3gpp");
        addFileType("3GPP", FILE_TYPE_3GPP, "video/3gpp");
        addFileType("3G2", FILE_TYPE_3GPP2, "video/3gpp2");
        addFileType("3GPP2", FILE_TYPE_3GPP2, "video/3gpp2");
        addFileType("MKV", FILE_TYPE_MKV, "video/x-matroska");
        addFileType("WEBM", FILE_TYPE_MKV, "video/x-matroska");
        addFileType("TS", FILE_TYPE_MP2TS, "video/mp2ts");
        addFileType("MPG", FILE_TYPE_MP2TS, "video/mp2ts");

        addFileType("AVI", FILE_TYPE_AVI, "video/avi");
        addFileType("DIVX", FILE_TYPE_DIVX, "video/divx");
        addFileType("WEBM", FILE_TYPE_WEBM, "video/webm");
        if (isWMVEnabled()) {
            addFileType("WMV", FILE_TYPE_WMV, "video/x-ms-wmv");
            addFileType("ASF", FILE_TYPE_ASF, "video/x-ms-asf");
        }

        addFileType("JPG", FILE_TYPE_JPEG, "image/jpeg");
        addFileType("JPEG", FILE_TYPE_JPEG, "image/jpeg");
        addFileType("GIF", FILE_TYPE_GIF, "image/gif");
        addFileType("PNG", FILE_TYPE_PNG, "image/png");
        addFileType("BMP", FILE_TYPE_BMP, "image/x-ms-bmp");
        addFileType("WBMP", FILE_TYPE_WBMP, "image/vnd.wap.wbmp");
 
        addFileType("M3U", FILE_TYPE_M3U, "audio/x-mpegurl");
        addFileType("PLS", FILE_TYPE_PLS, "audio/x-scpls");
        addFileType("WPL", FILE_TYPE_WPL, "application/vnd.ms-wpl");
        addFileType("AC3", FILE_TYPE_AC3, "audio/ac3");

        // compute file extensions list for native Media Scanner
        StringBuilder builder = new StringBuilder();
        Iterator<String> iterator = sFileTypeMap.keySet().iterator();
        
        while (iterator.hasNext()) {
            if (builder.length() > 0) {
                builder.append(',');
            }
            builder.append(iterator.next());
        } 
        sFileExtensions = builder.toString();
    }
    
    public static boolean isAudioFileType(int fileType) {
        return ((fileType >= FIRST_AUDIO_FILE_TYPE &&
                fileType <= LAST_AUDIO_FILE_TYPE) ||
                (fileType >= FIRST_MIDI_FILE_TYPE &&
                fileType <= LAST_MIDI_FILE_TYPE));
    }
    
    public static boolean isVideoFileType(int fileType) {
        return (fileType >= FIRST_VIDEO_FILE_TYPE &&
                fileType <= LAST_VIDEO_FILE_TYPE);
    }
    
    public static boolean isImageFileType(int fileType) {
        return (fileType >= FIRST_IMAGE_FILE_TYPE &&
                fileType <= LAST_IMAGE_FILE_TYPE);
    }
    
    public static boolean isPlayListFileType(int fileType) {
        return (fileType >= FIRST_PLAYLIST_FILE_TYPE &&
                fileType <= LAST_PLAYLIST_FILE_TYPE);
    }
    
    public static MediaFileType getFileType(String path) {
        int lastDot = path.lastIndexOf(".");
        if (lastDot < 0)
            return null;
        return sFileTypeMap.get(path.substring(lastDot + 1).toUpperCase());
    }
    
    public static int getFileTypeForMimeType(String mimeType) {
        Integer value = sMimeTypeMap.get(mimeType);
        return (value == null ? 0 : value.intValue());
    }

}
