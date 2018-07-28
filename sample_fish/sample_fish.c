/******************************************************************************
  Copyright (C), 2001-2017, Allwinner Tech. Co., Ltd.
 ******************************************************************************
  File Name     : sample_fish.c
  Version       : Initial Draft
  Author        : Allwinner BU3-PD2 Team
  Created       : 2017/1/5
  Last Modified :
  Description   : mpp component implement
  Function List :
  History       :
******************************************************************************/

#define LOG_TAG "sample_fish"
#include <endian.h>
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <pthread.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <cdx_list.h>
#include <utils/plat_log.h>
#include "mpi_ise_common.h"
#include "media/mm_comm_ise.h"
#include "media/mpi_ise.h"
#include "media/mpi_sys.h"
#include "ion_memmanager.h"
#include "sc_interface.h"
#include "memoryAdapter.h"
#include <tsemaphore.h>

#include <confparser.h>
#include "sample_fish.h"
#include "sample_fish_config.h"

#define Save_Picture         1
#define Dynamic_PTZ          0
#define Load_Len_Parameter   0
BOOL Thread_EXIT;
typedef struct awPic_Cap_s {
    int PicWidth;
    int PicHeight;
    int PicStride;
    int PicFrameRate;
    FILE* PicFilePath;
    pthread_t thread_id;
} awPic_Cap_s;

typedef struct awISE_PortCap_S {
    ISE_GRP  ISE_Group;
    ISE_CHN  ISE_Port;
    int width;
    int height;
    char *OutputFilePath;
    ISE_CHN_ATTR_S PortAttr;
    AW_S32   s32MilliSec;
    pthread_t thread_id;
} ISE_PortCap_S;

typedef struct awISE_pGroupCap_S {
    ISE_GRP ISE_Group;
    ISE_GROUP_ATTR_S pGrpAttr;
    ISE_PortCap_S PortCap_S[4];
} ISE_GroupCap_S;

typedef struct SampleFishFrameNode {
    VIDEO_FRAME_INFO_S mFrame;
    struct list_head mList;
} SampleFishFrameNode;

typedef struct SampleFishFrameManager {
    struct list_head mIdleList; //SampleFishFrameNode
    struct list_head mUsingList;
    pthread_mutex_t mWaitFrameLock;
    int mbWaitFrameFlag;
    cdx_sem_t mSemFrameCome;
    int mNodeCnt;
    pthread_mutex_t mLock;
    VIDEO_FRAME_INFO_S* (*PrefetchFirstIdleFrame)(void *pThiz);
    int (*UseFrame)(void *pThiz, VIDEO_FRAME_INFO_S *pFrame);
    int (*ReleaseFrame)(void *pThiz, unsigned int nFrameId);
} SampleFishFrameManager;

typedef struct SampleFishParameter {
    int Process_Count;
    ISE_GroupCap_S pISEGroupCap[4];
    SampleFishFrameManager  mFrameManager;
    awPic_Cap_s    PictureCap;
} SampleFishParameter;

VIDEO_FRAME_INFO_S* SampleFishFrameManager_PrefetchFirstIdleFrame(void *pThiz)
{
    SampleFishFrameManager *pFrameManager = (SampleFishFrameManager*)pThiz;
    SampleFishFrameNode *pFirstNode;
    VIDEO_FRAME_INFO_S *pFrameInfo;
    pthread_mutex_lock(&pFrameManager->mLock);
    if(!list_empty(&pFrameManager->mIdleList)) {
        pFirstNode = list_first_entry(&pFrameManager->mIdleList, SampleFishFrameNode, mList);
        pFrameInfo = &pFirstNode->mFrame;
    } else {
        pFrameInfo = NULL;
    }
    pthread_mutex_unlock(&pFrameManager->mLock);
    return pFrameInfo;
}

int SampleFishFrameManager_UseFrame(void *pThiz, VIDEO_FRAME_INFO_S *pFrame)
{
    int ret = 0;
    SampleFishFrameManager *pFrameManager = (SampleFishFrameManager*)pThiz;
    if(NULL == pFrame) {
        aloge("fatal error! pNode == NULL!");
        return -1;
    }
    pthread_mutex_lock(&pFrameManager->mLock);
    SampleFishFrameNode *pFirstNode = list_first_entry_or_null(&pFrameManager->mIdleList, SampleFishFrameNode, mList);
    if(pFirstNode) {
        if(&pFirstNode->mFrame == pFrame) {
            list_move_tail(&pFirstNode->mList, &pFrameManager->mUsingList);
        } else {
            aloge("fatal error! node is not match [%p]!=[%p]", pFrame, &pFirstNode->mFrame);
            ret = -1;
        }
    } else {
        aloge("fatal error! idle list is empty");
        ret = -1;
    }
    pthread_mutex_unlock(&pFrameManager->mLock);
    return ret;
}

int SampleFishFrameManager_ReleaseFrame(void *pThiz, unsigned int nFrameId)
{
    int ret = 0;
    SampleFishFrameManager *pFrameManager = (SampleFishFrameManager*)pThiz;
    pthread_mutex_lock(&pFrameManager->mLock);
    int bFindFlag = 0;
    SampleFishFrameNode *pEntry, *pTmp;
    list_for_each_entry_safe(pEntry, pTmp, &pFrameManager->mUsingList, mList) {
        if(pEntry->mFrame.mId == nFrameId) {
            list_move_tail(&pEntry->mList, &pFrameManager->mIdleList);
            bFindFlag = 1;
            break;
        }
    }
    if(0 == bFindFlag) {
        aloge("fatal error! frameId[%d] is not find", nFrameId);
        ret = -1;
    }
    pthread_mutex_unlock(&pFrameManager->mLock);
    return ret;
}

static ERRORTYPE SampleFishCallbackWrapper(void *cookie,MPP_CHN_S *Port, MPP_EVENT_TYPE event, void *pEventData)
{
    ERRORTYPE ret = SUCCESS;
    SampleFishFrameManager *pContext = (SampleFishFrameManager*)cookie;
    if(MOD_ID_ISE == Port->mModId) {
        switch(event) {
        case MPP_EVENT_RELEASE_ISE_VIDEO_BUFFER0: {
            VIDEO_FRAME_INFO_S *pVideoFrameInfo = (VIDEO_FRAME_INFO_S*)pEventData;
            pContext->ReleaseFrame(pContext, pVideoFrameInfo->mId);
            pthread_mutex_lock(&pContext->mWaitFrameLock);
            if(pContext->mbWaitFrameFlag) {
                pContext->mbWaitFrameFlag = 0;
                cdx_sem_up(&pContext->mSemFrameCome);
            }
            pthread_mutex_unlock(&pContext->mWaitFrameLock);
            break;
        }
        default: {
            aloge("fatal error! unknown event[0x%x] from channel[0x%x][0x%x][0x%x]!",
                  event, Port->mModId, Port->mDevId, Port->mChnId);
            ret = FALSE;
            break;
        }
        }
    }
    return ret;
}

int initSampleFishFrameManager(SampleFishParameter *pFishParameter, int nFrameNum)
{
    int ret = 0;
    ret = pthread_mutex_init(&pFishParameter->mFrameManager.mLock, NULL);
    if(ret!=0) {
        aloge("fatal error! pthread mutex init fail!");
        return ret;
    }
    ret = pthread_mutex_init(&pFishParameter->mFrameManager.mWaitFrameLock, NULL);
    if(ret != 0) {
        aloge("fatal error! pthread mutex init fail!");
        return ret;
    }
    ret = cdx_sem_init(&pFishParameter->mFrameManager.mSemFrameCome, 0);
    if(ret != 0) {
        aloge("cdx sem init fail!");
        return ret;
    }
    Thread_EXIT = FALSE;
    INIT_LIST_HEAD(&pFishParameter->mFrameManager.mIdleList);
    INIT_LIST_HEAD(&pFishParameter->mFrameManager.mUsingList);

    FILE* fd = NULL;
    fd = pFishParameter->PictureCap.PicFilePath;

    int width = 0, height = 0;
    width = pFishParameter->PictureCap.PicWidth;
    height = pFishParameter->PictureCap.PicHeight;
    int i = 0;
    SampleFishFrameNode *pNode;
    unsigned int uPhyAddr;
    void *pVirtAddr;
    unsigned int block_size = 0;
    unsigned int read_size = 0;
    ret = ion_memOpen();
    if (ret != 0) {
        aloge("Open ion failed!");
        return ret;
    }
    for(i=0; i<nFrameNum; i++) {
        pNode = (SampleFishFrameNode*)malloc(sizeof(SampleFishFrameNode));
        memset(pNode, 0, sizeof(SampleFishFrameNode));
        pNode->mFrame.mId = i;
        pNode->mFrame.VFrame.mpVirAddr[0] = ion_allocMem(width * height *1.5);
        if (pNode->mFrame.VFrame.mpVirAddr[0] == NULL) {
            aloge("allocMem error!");
            return -1;
        }
        memset(pNode->mFrame.VFrame.mpVirAddr[0], 0x0, width * height * 1.5);
        pNode->mFrame.VFrame.mPhyAddr[0] = (unsigned int)ion_getMemPhyAddr(pNode->mFrame.VFrame.mpVirAddr[0]);
        pNode->mFrame.VFrame.mpVirAddr[1] = pNode->mFrame.VFrame.mpVirAddr[0] + width * height;
        pNode->mFrame.VFrame.mPhyAddr[1] = pNode->mFrame.VFrame.mPhyAddr[0] + width * height;
        block_size = width * height * sizeof(unsigned char);
        read_size = fread(pNode->mFrame.VFrame.mpVirAddr[0], 1 ,block_size, fd);
        if (read_size < 0) {
            aloge("read yuv file fail\n");
            fclose(fd);
            fd = NULL;
            return -1;
        }
        block_size =  width * height * sizeof(unsigned char) / 2;
        read_size = fread(pNode->mFrame.VFrame.mpVirAddr[1], 1, block_size, fd);
        if (read_size < 0) {
            aloge("read yuv file fail\n");
            fclose(fd);
            fd = NULL;
            return -1;
        }
        ion_flushCache(pNode->mFrame.VFrame.mpVirAddr[0],width * height *1.5);
        pNode->mFrame.VFrame.mWidth = width;
        pNode->mFrame.VFrame.mHeight = height;
        list_add_tail(&pNode->mList, &pFishParameter->mFrameManager.mIdleList);
    }
    fclose(fd);
    pFishParameter->mFrameManager.mNodeCnt = nFrameNum;
    pFishParameter->mFrameManager.PrefetchFirstIdleFrame = SampleFishFrameManager_PrefetchFirstIdleFrame;
    pFishParameter->mFrameManager.UseFrame = SampleFishFrameManager_UseFrame;
    pFishParameter->mFrameManager.ReleaseFrame = SampleFishFrameManager_ReleaseFrame;
    return 0;
}

int destroySampleFishFrameManager(SampleFishParameter *pFishParameter)
{
    if(!list_empty(&pFishParameter->mFrameManager.mUsingList)) {
        aloge("fatal error! why using list is not empty");
    }
    int cnt = 0;
    struct list_head *pList;
    list_for_each(pList, &pFishParameter->mFrameManager.mIdleList) {
        cnt++;
    }
    if(cnt != pFishParameter->mFrameManager.mNodeCnt) {
        aloge("fatal error! frame count is not match [%d]!=[%d]", cnt,
              pFishParameter->mFrameManager.mNodeCnt);
    }
    SampleFishFrameNode *pEntry, *pTmp;
    list_for_each_entry_safe(pEntry, pTmp, &pFishParameter->mFrameManager.mIdleList, mList) {
        ion_freeMem(pEntry->mFrame.VFrame.mpVirAddr[0]);
        list_del(&pEntry->mList);
        free(pEntry);
    }
    pthread_mutex_destroy(&pFishParameter->mFrameManager.mLock);
    return 0;
}

/*MPI ise*/
int aw_iseport_creat(ISE_GRP IseGrp, ISE_CHN IsePort, ISE_CHN_ATTR_S *PortAttr)
{
    int ret = -1;
    ret = AW_MPI_ISE_CreatePort(IseGrp, IsePort, PortAttr);
    if(ret < 0) {
        aloge("Create ISE Port failed,IseGrp= %d,IsePort = %d",IseGrp,IsePort);
        return ret ;
    }
    ret = AW_MPI_ISE_SetPortAttr(IseGrp,IsePort,PortAttr);
    if(ret < 0) {
        aloge("Set ISE Port Attr failed,IseGrp= %d,IsePort = %d",IseGrp,IsePort);
        return ret ;
    }
    return 0;
}

int aw_iseport_destory(ISE_GRP IseGrp, ISE_CHN IsePort)
{
    int ret = -1;
    ret = AW_MPI_ISE_DestroyPort(IseGrp,IsePort);
    if(ret < 0) {
        aloge("Destory ISE Port failed, IseGrp= %d,IsePort = %d",IseGrp,IsePort);
        return ret ;
    }
    return 0;
}

int aw_isegroup_creat(ISE_GRP IseGrp, ISE_GROUP_ATTR_S *pGrpAttr)
{
    int ret = -1;
    ret = AW_MPI_ISE_CreateGroup(IseGrp, pGrpAttr);
    if(ret < 0) {
        aloge("Create ISE Group failed, IseGrp= %d",IseGrp);
        return ret ;
    }
    ret = AW_MPI_ISE_SetGrpAttr(IseGrp, pGrpAttr);
    if(ret < 0) {
        aloge("Set ISE GrpAttr failed, IseGrp= %d",IseGrp);
        return ret ;
    }
    return 0;
}

int aw_isegroup_destory(ISE_GRP IseGrp)
{
    int ret = -1;
    ret = AW_MPI_ISE_DestroyGroup(IseGrp);
    if(ret < 0) {
        aloge("Destroy ISE Group failed, IseGrp= %d",IseGrp);
        return ret ;
    }
    return 0;
}

static int ParseCmdLine(int argc, char **argv, SampleFishCmdLineParam *pCmdLinePara)
{
    alogd("sample_fish path:[%s], arg number is [%d]", argv[0], argc);
    int ret = 0;
    int i=1;
    memset(pCmdLinePara, 0, sizeof(SampleFishCmdLineParam));
    while(i < argc) {
        if(!strcmp(argv[i], "-path")) {
            if(++i >= argc) {
                aloge("fatal error! use -h to learn how to set parameter!!!");
                ret = -1;
                break;
            }
            if(strlen(argv[i]) >= MAX_FILE_PATH_SIZE) {
                aloge("fatal error! file path[%s] too long: [%d]>=[%d]!", argv[i], strlen(argv[i]), MAX_FILE_PATH_SIZE);
            }
            strncpy(pCmdLinePara->mConfigFilePath, argv[i], MAX_FILE_PATH_SIZE-1);
            pCmdLinePara->mConfigFilePath[MAX_FILE_PATH_SIZE-1] = '\0';
        } else if(!strcmp(argv[i], "-h")) {
            alogd("CmdLine param:\n"
                  "\t-path /home/sample_fish.conf\n");
            ret = 1;
            break;
        } else {
            alogd("ignore invalid CmdLine param:[%s], type -h to get how to set parameter!", argv[i]);
        }
        i++;
    }
    return ret;
}

static ERRORTYPE loadSampleFishConfig(SampleFishConfig *pConfig, const char *conf_path)
{
    int ret;
    char *ptr = NULL;
    char *pStrPixelFormat = NULL,*EncoderType = NULL;
    int i = 0,ISEPortNum = 0;
    CONFPARSER_S stConfParser;
    char name[256];
    ret = createConfParser(conf_path, &stConfParser);
    if(ret < 0) {
        aloge("load conf fail");
        return FAILURE;
    }
    memset(pConfig, 0, sizeof(SampleFishConfig));
    pConfig->AutoTestCount = GetConfParaInt(&stConfParser, SAMPLE_FISH_Auto_Test_Count, 0);
    pConfig->Process_Count = GetConfParaInt(&stConfParser, SAMPLE_FISH_Process_Count, 0);

    /* Get Picture parameter*/
    pConfig->PicConfig.PicFrameRate = GetConfParaInt(&stConfParser, SAMPLE_Fish_Pic_Frame_Rate, 0);
    pConfig->PicConfig.PicWidth = GetConfParaInt(&stConfParser, SAMPLE_Fish_Pic_Width, 0);
    pConfig->PicConfig.PicHeight = GetConfParaInt(&stConfParser, SAMPLE_Fish_Pic_Height, 0);
    pConfig->PicConfig.PicStride = GetConfParaInt(&stConfParser, SAMPLE_Fish_Pic_Stride, 0);
    alogd("pic_width = %d, pic_height = %d, pic_frame_rate = %d\n",
          pConfig->PicConfig.PicWidth,pConfig->PicConfig.PicHeight,pConfig->PicConfig.PicFrameRate);
    ptr = (char*)GetConfParaString(&stConfParser, SAMPLE_Fish_Pic_File_Path, NULL);
    strncpy(pConfig->PicConfig.PicFilePath, ptr, MAX_FILE_PATH_SIZE-1);
    pConfig->PicConfig.PicFilePath[MAX_FILE_PATH_SIZE-1] = '\0';

    /* Get ISE parameter*/
    pConfig->ISEGroupConfig.ISEPortNum = GetConfParaInt(&stConfParser, SAMPLE_Fish_ISE_Port_Num, 0);
    ISEPortNum = pConfig->ISEGroupConfig.ISEPortNum;
    ptr = (char*)GetConfParaString(&stConfParser, SAMPLE_Fish_ISE_Output_File_Path, NULL);
    strncpy(pConfig->ISEGroupConfig.OutputFilePath, ptr, MAX_FILE_PATH_SIZE-1);
    pConfig->ISEGroupConfig.OutputFilePath[MAX_FILE_PATH_SIZE-1] = '\0';
    alogd("ise output_file_path = %s\n",pConfig->ISEGroupConfig.OutputFilePath);

    pConfig->ISEGroupConfig.ISE_Dewarp_Mode = GetConfParaInt(&stConfParser, SAMPLE_Fish_Dewarp_Mode, 0);
    pConfig->ISEGroupConfig.Lens_Parameter_P = pConfig->PicConfig.PicWidth/3.1415;
    pConfig->ISEGroupConfig.Lens_Parameter_Cx = pConfig->PicConfig.PicWidth/2;
    pConfig->ISEGroupConfig.Lens_Parameter_Cy = pConfig->PicConfig.PicHeight/2;
    pConfig->ISEGroupConfig.Mount_Mode = GetConfParaInt(&stConfParser, SAMPLE_Fish_Mount_Mode, 0);
    pConfig->ISEGroupConfig.normal_pan = GetConfParaInt(&stConfParser, SAMPLE_Fish_NORMAL_Pan, 0);
    pConfig->ISEGroupConfig.normal_tilt = GetConfParaInt(&stConfParser, SAMPLE_Fish_NORMAL_Tilt, 0);
    pConfig->ISEGroupConfig.normal_zoom = GetConfParaInt(&stConfParser,   SAMPLE_Fish_NORMAL_Zoom, 0);
    alogd("ISE Group Parameter:dewarp_mode = %d,Lens_Parameter_p = %f,Lens_Parameter_cx = %d,Lens_Parameter_cy = %d,"
          "mount_mode = %d,pan = %d,tilt = %d,zoom = %d\n",pConfig->ISEGroupConfig.ISE_Dewarp_Mode,
          pConfig->ISEGroupConfig.Lens_Parameter_P,pConfig->ISEGroupConfig.Lens_Parameter_Cx,
          pConfig->ISEGroupConfig.Lens_Parameter_Cy,pConfig->ISEGroupConfig.Mount_Mode,
          pConfig->ISEGroupConfig.normal_pan,pConfig->ISEGroupConfig.normal_tilt,pConfig->ISEGroupConfig.normal_zoom);
    /*ISE Port parameter*/
    for(i = 0; i < ISEPortNum; i++) {
        snprintf(name, 256, "ise_port%d_width", i);
        pConfig->ISEPortConfig[i].ISEWidth = GetConfParaInt(&stConfParser, name, 0);
        snprintf(name, 256, "ise_port%d_height", i);
        pConfig->ISEPortConfig[i].ISEHeight = GetConfParaInt(&stConfParser, name, 0);
        snprintf(name, 256, "ise_port%d_stride", i);
        pConfig->ISEPortConfig[i].ISEStride = GetConfParaInt(&stConfParser, name, 0);
        snprintf(name, 256, "ise_port%d_flip_enable", i);
        pConfig->ISEPortConfig[i].flip_enable = GetConfParaInt(&stConfParser, name, 0);
        snprintf(name, 256, "ise_port%d_mirror_enable", i);
        pConfig->ISEPortConfig[i].mirror_enable = GetConfParaInt(&stConfParser, name, 0);
        alogd("ISE Port%d Parameter:ISE_Width = %d,ISE_Height = %d,ISE_Stride = %d,"
              "ISE_flip_enable = %d,mirror_enable = %d\n",i,pConfig->ISEPortConfig[i].ISEWidth,
              pConfig->ISEPortConfig[i].ISEHeight,pConfig->ISEPortConfig[i].ISEStride,
              pConfig->ISEPortConfig[i].flip_enable,pConfig->ISEPortConfig[i].mirror_enable);
    }
    destroyConfParser(&stConfParser);
    return SUCCESS;
}

static void *Loop_SendImageFileThread(void *pArg)
{
    alogd("Start Loop_SendImageFileThread");
    SampleFishParameter *pCap = (SampleFishParameter *)pArg;
    ISE_GRP ISEGroup = pCap->pISEGroupCap[0].ISE_Group;
    int ret = 0;
    int i = 0;
    int s32MilliSec = -1;
    int framerate = pCap->PictureCap.PicFrameRate;
    VIDEO_FRAME_INFO_S *pFrameInfo = NULL;
    ret = AW_MPI_ISE_Start(ISEGroup);
    if(ret < 0) {
        aloge("ise start error!\n");
        return (void*)ret;
    }

    while((i != pCap->Process_Count) || (pCap->Process_Count == -1)) {
        //request idle frame
        pFrameInfo = pCap->mFrameManager.PrefetchFirstIdleFrame(&pCap->mFrameManager);
        if(NULL == pFrameInfo) {
            pthread_mutex_lock(&pCap->mFrameManager.mWaitFrameLock);
            pFrameInfo = pCap->mFrameManager.PrefetchFirstIdleFrame(&pCap->mFrameManager);
            if(pFrameInfo!=NULL) {
                pthread_mutex_unlock(&pCap->mFrameManager.mWaitFrameLock);
            } else {
                pCap->mFrameManager.mbWaitFrameFlag = 1;
                pthread_mutex_unlock(&pCap->mFrameManager.mWaitFrameLock);
                cdx_sem_down_timedwait(&pCap->mFrameManager.mSemFrameCome, 500);
                continue;
            }
        }
        pCap->mFrameManager.UseFrame(&pCap->mFrameManager, pFrameInfo);
        ret = AW_MPI_ISE_SendPic(ISEGroup, pFrameInfo, NULL, s32MilliSec);
        if(ret != SUCCESS) {
            alogd("impossible, send frameId[%d] fail?", pFrameInfo->mId);
            pCap->mFrameManager.ReleaseFrame(&pCap->mFrameManager, pFrameInfo->mId);
            continue;
        }
        i++;
        usleep(framerate);
    }
    Thread_EXIT = TRUE;
    return NULL;
}

static void *Loop_GetIseData(void *pArg)
{
    alogd("Loop Start %s.\r\n", __func__);
    int i = 0,ret = 0,j =0;
    int width = 0,height = 0;
    BOOL Dynamic_Flag = FALSE;
    ISE_CHN_ATTR_S ISEPortAttr;
    ISE_PortCap_S *pCap = (ISE_PortCap_S *)pArg;
    ISE_GRP ISEGroup = pCap->ISE_Group;
    ISE_CHN ISEPort = pCap->ISE_Port;
    pthread_t thread_id = pCap->thread_id;
    int s32MilliSec = pCap->s32MilliSec;
    VIDEO_FRAME_INFO_S ISE_Frame_buffer;
    // YUV Out
    width = pCap->width;
    height = pCap->height;
    char *name = pCap->OutputFilePath;
    while(1) {
        if(Thread_EXIT == TRUE) {
            alogd("Thread_EXIT is True!\n");
            break;
        }
        if ((ret = AW_MPI_ISE_GetData(ISEGroup, ISEPort, &ISE_Frame_buffer, s32MilliSec)) < 0) {
            aloge("ISE Port%d get data failed!\n",ISEPort);
            continue ;
        } else {
            j++;
            if (j % 30 == 0) {
                time_t now;
                struct tm *timenow;
                time(&now);
                timenow = localtime(&now);
                alogd("Cap threadid = 0x%lx,port = %d; local time is %s\r\n",
                      thread_id, ISEPort, asctime(timenow));
            }

#if Save_Picture
            if(i % 10 == 0) {
                char filename[125];
                sprintf(filename,"/%s/fish_ch%d_%d.yuv",name,ISEPort,i);
                FILE *fd = NULL;
                fd = fopen(filename,"wb+");
                fwrite(ISE_Frame_buffer.VFrame.mpVirAddr[0], width * height, 1, fd);
                fwrite(ISE_Frame_buffer.VFrame.mpVirAddr[1], ((width * height)>>1) , 1, fd);
                fclose(fd);
            }
#endif
            AW_MPI_ISE_ReleaseData(ISEGroup, ISEPort, &ISE_Frame_buffer);
            i++;

#if Dynamic_PTZ
            struct timeval tpstart,tpend;
            float timeuse;
            gettimeofday(&tpstart, NULL);
            if(ISEPort == 0 && !Dynamic_Flag) {
                ISEPortAttr.mode_attr.mFish.ise_cfg.pan = 125.0f;
                ISEPortAttr.mode_attr.mFish.ise_cfg.tilt = 45.0f;
                ISEPortAttr.mode_attr.mFish.ise_cfg.zoom = 2.0f;
                AW_MPI_ISE_SetPortAttr(ISEGroup,ISEPort,&ISEPortAttr);
//                Dynamic_Flag = TRUE;
            }
            gettimeofday(&tpend,NULL);
            timeuse=1000000*(tpend.tv_sec-tpstart.tv_sec)+(tpend.tv_usec-tpstart.tv_usec);
            timeuse/=1000;
            alogd("------>Dynamic PTZ Used Time:%f ms<------\n", timeuse);
#endif
        }
    }
    return NULL;
}

void Sample_Fish_HELP()
{
    alogd("Run sample_fish command: ./sample_fish -path ./sample_fish.conf\r\n");
}

int main(int argc, char *argv[])
{
    int ret = 0, i = 0, j = 0, count = 0;
    int result = 0;
    int AutoTestCount = 0;
    alogd("Sample fish buile time = %s, %s.\r\n", __DATE__, __TIME__);
    if(argc != 3) {
        Sample_Fish_HELP();
        exit(0);
    }

    SampleFishParameter FisheyePara;
    SampleFishConfparser stContext;

    //parse command line param,read sample_virvi2fish2vo.conf
    if(ParseCmdLine(argc, argv, &stContext.mCmdLinePara) != 0) {
        aloge("fatal error! command line param is wrong, exit!");
        result = -1;
        goto _exit;
    }
    char *pConfigFilePath;
    if(strlen(stContext.mCmdLinePara.mConfigFilePath) > 0) {
        pConfigFilePath = stContext.mCmdLinePara.mConfigFilePath;
    } else {
        pConfigFilePath = DEFAULT_SAMPLE_FISH_CONF_PATH;
    }
    //parse config file.
    if(loadSampleFishConfig(&stContext.mConfigPara, pConfigFilePath) != SUCCESS) {
        aloge("fatal error! no config file or parse conf file fail");
        result = -1;
        goto _exit;
    }
    AutoTestCount = stContext.mConfigPara.AutoTestCount;
    FisheyePara.Process_Count = stContext.mConfigPara.Process_Count;
    while (count != AutoTestCount) {
        /* start mpp systerm */
        MPP_SYS_CONF_S mSysConf;
        mSysConf.nAlignWidth = 32;
        AW_MPI_SYS_SetConf(&mSysConf);
        ret = AW_MPI_SYS_Init();
        if (ret < 0) {
            aloge("sys init failed");
            result = -1;
            goto sys_exit;
        }

        //init frame manager
        FisheyePara.PictureCap.PicWidth = stContext.mConfigPara.PicConfig.PicWidth;
        FisheyePara.PictureCap.PicHeight = stContext.mConfigPara.PicConfig.PicHeight;
        FisheyePara.PictureCap.PicStride = stContext.mConfigPara.PicConfig.PicStride;
        FisheyePara.PictureCap.PicFrameRate = ((float)1/stContext.mConfigPara.PicConfig.PicFrameRate) * 1000000;
        FisheyePara.PictureCap.PicFilePath = fopen(stContext.mConfigPara.PicConfig.PicFilePath,"rb");
        ret = initSampleFishFrameManager(&FisheyePara, 10);
        if(ret < 0) {
            aloge("Init FrameManager failed!");
            goto destory_framemanager;
        }

        int ISEPortNum = 0;
        ISEPortNum = stContext.mConfigPara.ISEGroupConfig.ISEPortNum;
        ISE_PortCap_S *pISEPortCap;
#if  Load_Len_Parameter
        pISEPortCap = &FisheyePara.pISEGroupCap[0].PortCap_S[0];
        FILE *ise_cfg_fd = NULL;
        ise_cfg_fd = fopen("./mo_cfg.bin","rb+");
        fread(&pISEPortCap->PortAttr.mode_attr.mFish.ise_cfg.p,sizeof(float),1,ise_cfg_fd);
        fread(&pISEPortCap->PortAttr.mode_attr.mFish.ise_cfg.cx,sizeof(float),1,ise_cfg_fd);
        fread(&pISEPortCap->PortAttr.mode_attr.mFish.ise_cfg.cy,sizeof(float),1,ise_cfg_fd);
        alogd("load mo len parameter:p = %f, cx = %f, cy = %f",
              pISEPortCap->PortAttr.mode_attr.mFish.ise_cfg.p,
              pISEPortCap->PortAttr.mode_attr.mFish.ise_cfg.cx,
              pISEPortCap->PortAttr.mode_attr.mFish.ise_cfg.cy);
#endif
        for(j = 0; j < 1; j++) {
            /*Set ISE Group Attribute*/
            memset(&FisheyePara.pISEGroupCap[j], 0, sizeof(ISE_GroupCap_S));
            FisheyePara.pISEGroupCap[j].ISE_Group = j;
            FisheyePara.pISEGroupCap[j].pGrpAttr.iseMode = ISEMODE_ONE_FISHEYE;
            ret = aw_isegroup_creat(FisheyePara.pISEGroupCap[j].ISE_Group, &FisheyePara.pISEGroupCap[j].pGrpAttr);
            if(ret < 0) {
                aloge("ISE Group %d creat failed",FisheyePara.pISEGroupCap[j].ISE_Group);
                goto destory_isegroup;
            }
            for(i = 0; i< ISEPortNum; i++) {
                /*Set ISE Port Attribute*/
                memset(&FisheyePara.pISEGroupCap[j].PortCap_S[i], 0, sizeof(ISE_PortCap_S));
                FisheyePara.pISEGroupCap[j].PortCap_S[i].ISE_Group = j;
                FisheyePara.pISEGroupCap[j].PortCap_S[i].ISE_Port = i;
                FisheyePara.pISEGroupCap[j].PortCap_S[i].thread_id = i;
                FisheyePara.pISEGroupCap[j].PortCap_S[i].s32MilliSec = 4000;
                FisheyePara.pISEGroupCap[j].PortCap_S[i].OutputFilePath =  stContext.mConfigPara.ISEGroupConfig.OutputFilePath;
                pISEPortCap = &FisheyePara.pISEGroupCap[j].PortCap_S[i];
                if(i == 0) { //fish arttr
                    pISEPortCap->PortAttr.mode_attr.mFish.ise_cfg.dewarp_mode = stContext.mConfigPara.ISEGroupConfig.ISE_Dewarp_Mode;
                    pISEPortCap->PortAttr.mode_attr.mFish.ise_cfg.in_h = stContext.mConfigPara.PicConfig.PicHeight;
                    pISEPortCap->PortAttr.mode_attr.mFish.ise_cfg.in_w = stContext.mConfigPara.PicConfig.PicWidth;
                    pISEPortCap->PortAttr.mode_attr.mFish.ise_cfg.p  = stContext.mConfigPara.ISEGroupConfig.Lens_Parameter_P;
                    pISEPortCap->PortAttr.mode_attr.mFish.ise_cfg.cx = stContext.mConfigPara.ISEGroupConfig.Lens_Parameter_Cx;
                    pISEPortCap->PortAttr.mode_attr.mFish.ise_cfg.cy = stContext.mConfigPara.ISEGroupConfig.Lens_Parameter_P;
                    pISEPortCap->PortAttr.mode_attr.mFish.ise_cfg.in_yuv_type = 0; // YUV420
                    pISEPortCap->PortAttr.mode_attr.mFish.ise_cfg.out_yuv_type = 0; // YUV420
                    pISEPortCap->PortAttr.mode_attr.mFish.ise_cfg.in_luma_pitch = stContext.mConfigPara.PicConfig.PicWidth;
                    pISEPortCap->PortAttr.mode_attr.mFish.ise_cfg.in_chroma_pitch = stContext.mConfigPara.PicConfig.PicWidth;
                    if(pISEPortCap->PortAttr.mode_attr.mFish.ise_cfg.dewarp_mode == WARP_NORMAL) {
                        pISEPortCap->PortAttr.mode_attr.mFish.ise_cfg.mount_mode = stContext.mConfigPara.ISEGroupConfig.Mount_Mode;
                        pISEPortCap->PortAttr.mode_attr.mFish.ise_cfg.pan = stContext.mConfigPara.ISEGroupConfig.normal_pan;
                        pISEPortCap->PortAttr.mode_attr.mFish.ise_cfg.tilt = stContext.mConfigPara.ISEGroupConfig.normal_tilt;
                        pISEPortCap->PortAttr.mode_attr.mFish.ise_cfg.zoom = stContext.mConfigPara.ISEGroupConfig.normal_zoom;
                    }
                    if(pISEPortCap->PortAttr.mode_attr.mFish.ise_cfg.dewarp_mode == WARP_PANO360) {
                        pISEPortCap->PortAttr.mode_attr.mFish.ise_cfg.mount_mode = stContext.mConfigPara.ISEGroupConfig.Mount_Mode;
                    }
                    if(pISEPortCap->PortAttr.mode_attr.mFish.ise_cfg.dewarp_mode == WARP_180WITH2) {
                        pISEPortCap->PortAttr.mode_attr.mFish.ise_cfg.mount_mode = stContext.mConfigPara.ISEGroupConfig.Mount_Mode;
                    }
                    if(pISEPortCap->PortAttr.mode_attr.mFish.ise_cfg.dewarp_mode == WARP_UNDISTORT) {
#if 1
                        pISEPortCap->PortAttr.mode_attr.mFish.ise_cfg.cx = 918.18164 * (stContext.mConfigPara.PicConfig.PicWidth*1.0/1920);
                        pISEPortCap->PortAttr.mode_attr.mFish.ise_cfg.cy = 479.86784 * (stContext.mConfigPara.PicConfig.PicHeight*1.0/1080);
                        pISEPortCap->PortAttr.mode_attr.mFish.ise_cfg.fx  = 1059.11146 * (stContext.mConfigPara.PicConfig.PicWidth*1.0/1920);
                        pISEPortCap->PortAttr.mode_attr.mFish.ise_cfg.fy = 1053.26240 * (stContext.mConfigPara.PicConfig.PicHeight*1.0/1080);
                        pISEPortCap->PortAttr.mode_attr.mFish.ise_cfg.cxd = 876.34066 * (stContext.mConfigPara.PicConfig.PicWidth*1.0/1920);
                        pISEPortCap->PortAttr.mode_attr.mFish.ise_cfg.cyd = 465.93162 * (stContext.mConfigPara.PicConfig.PicHeight*1.0/1080);
                        pISEPortCap->PortAttr.mode_attr.mFish.ise_cfg.fxd = 691.76196 * (stContext.mConfigPara.PicConfig.PicWidth*1.0/1920);
                        pISEPortCap->PortAttr.mode_attr.mFish.ise_cfg.fyd = 936.82897 * (stContext.mConfigPara.PicConfig.PicHeight*1.0/1080);
                        pISEPortCap->PortAttr.mode_attr.mFish.ise_cfg.k[0] = 0.182044494560808;
                        pISEPortCap->PortAttr.mode_attr.mFish.ise_cfg.k[1] = -0.1481043082174997;
                        pISEPortCap->PortAttr.mode_attr.mFish.ise_cfg.k[2] = -0.005128687334715951;
                        pISEPortCap->PortAttr.mode_attr.mFish.ise_cfg.k[3] = 0.567926713301489;
                        pISEPortCap->PortAttr.mode_attr.mFish.ise_cfg.k[4] = -0.1789466261819578;
                        pISEPortCap->PortAttr.mode_attr.mFish.ise_cfg.k[5] = -0.03561367966855939;
                        pISEPortCap->PortAttr.mode_attr.mFish.ise_cfg.p_undis[0] = 0.000649146914880072;
                        pISEPortCap->PortAttr.mode_attr.mFish.ise_cfg.p_undis[1] = 0.0002534155740808075;
#endif

#if 1
                        pISEPortCap->PortAttr.mode_attr.mFish.ise_cfg.cx = 889.50411 * (stContext.mConfigPara.PicConfig.PicWidth*1.0/1920);
                        pISEPortCap->PortAttr.mode_attr.mFish.ise_cfg.cy = 522.42100 * (stContext.mConfigPara.PicConfig.PicHeight*1.0/1080);
                        pISEPortCap->PortAttr.mode_attr.mFish.ise_cfg.fx  = 963.98364 * (stContext.mConfigPara.PicConfig.PicWidth*1.0/1920);
                        pISEPortCap->PortAttr.mode_attr.mFish.ise_cfg.fy = 965.10729 * (stContext.mConfigPara.PicConfig.PicHeight*1.0/1080);
                        pISEPortCap->PortAttr.mode_attr.mFish.ise_cfg.cxd = 801.04388 * (stContext.mConfigPara.PicConfig.PicWidth*1.0/1920);
                        pISEPortCap->PortAttr.mode_attr.mFish.ise_cfg.cyd = 518.79163 * (stContext.mConfigPara.PicConfig.PicHeight*1.0/1080);
                        pISEPortCap->PortAttr.mode_attr.mFish.ise_cfg.fxd = 580.58685 * (stContext.mConfigPara.PicConfig.PicWidth*1.0/1920);
                        pISEPortCap->PortAttr.mode_attr.mFish.ise_cfg.fyd = 850.71746 * (stContext.mConfigPara.PicConfig.PicHeight*1.0/1080);
                        pISEPortCap->PortAttr.mode_attr.mFish.ise_cfg.k[0] = 0.5874970340655806;
                        pISEPortCap->PortAttr.mode_attr.mFish.ise_cfg.k[1] = 0.01263866896598456;
                        pISEPortCap->PortAttr.mode_attr.mFish.ise_cfg.k[2] = -0.003440797814786819;
                        pISEPortCap->PortAttr.mode_attr.mFish.ise_cfg.k[3] = 0.9486826799125321;
                        pISEPortCap->PortAttr.mode_attr.mFish.ise_cfg.k[4] = 0.1349250696268053;
                        pISEPortCap->PortAttr.mode_attr.mFish.ise_cfg.k[5] = -0.01052234728693081;
                        pISEPortCap->PortAttr.mode_attr.mFish.ise_cfg.p_undis[0] = 0.000362407777916013;
                        pISEPortCap->PortAttr.mode_attr.mFish.ise_cfg.p_undis[1] = -0.0001245920435755955;
#endif
                    }
                }
                pISEPortCap->PortAttr.mode_attr.mFish.ise_cfg.out_en[i] = 1;
                pISEPortCap->PortAttr.mode_attr.mFish.ise_cfg.out_w[i] =  stContext.mConfigPara.ISEPortConfig[i].ISEWidth;
                pISEPortCap->PortAttr.mode_attr.mFish.ise_cfg.out_h[i] =  stContext.mConfigPara.ISEPortConfig[i].ISEHeight;
                pISEPortCap->PortAttr.mode_attr.mFish.ise_cfg.out_flip[i] =  stContext.mConfigPara.ISEPortConfig[i].flip_enable;
                pISEPortCap->PortAttr.mode_attr.mFish.ise_cfg.out_mirror[i] =  stContext.mConfigPara.ISEPortConfig[i].mirror_enable;
                pISEPortCap->PortAttr.mode_attr.mFish.ise_cfg.out_luma_pitch[i] =  stContext.mConfigPara.ISEPortConfig[i].ISEStride;
                pISEPortCap->PortAttr.mode_attr.mFish.ise_cfg.out_chroma_pitch[i] =  stContext.mConfigPara.ISEPortConfig[i].ISEStride;
                ret = aw_iseport_creat(FisheyePara.pISEGroupCap[j].ISE_Group, FisheyePara.pISEGroupCap[j].PortCap_S[i].ISE_Port,
                                       &pISEPortCap->PortAttr);
                if(ret < 0) {
                    aloge("ISE Port%d creat failed",FisheyePara.pISEGroupCap[j].PortCap_S[i].ISE_Port);
                    goto destory_iseport;
                }
                FisheyePara.pISEGroupCap[j].PortCap_S[i].width = pISEPortCap->PortAttr.mode_attr.mFish.ise_cfg.out_w[i];
                FisheyePara.pISEGroupCap[j].PortCap_S[i].height = pISEPortCap->PortAttr.mode_attr.mFish.ise_cfg.out_h[i];
            }
        }

        MPPCallbackInfo cbInfo;
        cbInfo.cookie = (void*)&FisheyePara.mFrameManager;
        cbInfo.callback = (MPPCallbackFuncType)&SampleFishCallbackWrapper;
        ret = AW_MPI_ISE_RegisterCallback(FisheyePara.pISEGroupCap[0].ISE_Group, &cbInfo);
        if(ret != SUCCESS) {
            aloge("Fish Register Callback error!\n");
            result = -1;
            goto _exit;
        }
//        ret = AW_MPI_ISE_SetISEFreq(FisheyePara.pISEGroupCap[0].ISE_Group,576);
        ret = pthread_create(&FisheyePara.PictureCap.thread_id, NULL, Loop_SendImageFileThread, (void *)&FisheyePara);
        if (ret < 0) {
            aloge("Loop_SendImageFileThread create failed");
            result = -1;
            goto _exit;
        }
        for(i = 0; i< ISEPortNum; i++) {
            pthread_create(&FisheyePara.pISEGroupCap[0].PortCap_S[i].thread_id, NULL,
                           Loop_GetIseData, (void *)&FisheyePara.pISEGroupCap[0].PortCap_S[i]);
        }

        for (i = 0; i < 1; i++) {
            pthread_join(FisheyePara.PictureCap.thread_id, NULL);
        }

        for(i = 0; i < ISEPortNum; i++) {
            pthread_join(FisheyePara.pISEGroupCap[0].PortCap_S[i].thread_id, NULL);
        }

        ret = AW_MPI_ISE_Stop(FisheyePara.pISEGroupCap[0].ISE_Group);
        if(ret < 0) {
            aloge("ise stop error!\n");
            result = -1;
            goto _exit;
        }

destory_iseport:
        for(i = 0; i < ISEPortNum; i++) {
            ret = aw_iseport_destory(FisheyePara.pISEGroupCap[0].ISE_Group,
                                     FisheyePara.pISEGroupCap[0].PortCap_S[i].ISE_Port);
            if(ret < 0) {
                aloge("ISE Port%d distory error!",FisheyePara.pISEGroupCap[0].PortCap_S[i].ISE_Port);
                result = -1;
                goto _exit;
            }
        }

destory_isegroup:
        ret = aw_isegroup_destory(FisheyePara.pISEGroupCap[0].ISE_Group);
        if(ret < 0) {
            aloge("ISE Destroy Group%d error!",FisheyePara.pISEGroupCap[0].ISE_Group);
            result = -1;
            goto _exit;
        }
destory_framemanager:
        destroySampleFishFrameManager(&FisheyePara);
        ret = ion_memClose();
        if (ret != 0) {
            aloge("Close ion failed!");
        }

sys_exit:
        /* exit mpp systerm */
        ret = AW_MPI_SYS_Exit();
        if (ret < 0) {
            aloge("sys exit failed!");
            result = -1;
            goto _exit;
        }
        alogd("======================================.\r\n");
        alogd("Auto Test count end: %d. (MaxCount==1000).\r\n", count);
        alogd("======================================.\r\n");
        count ++;
    }
    alogd("sample_fish exit!\n");
    return 0;
_exit:
    return result;
}


