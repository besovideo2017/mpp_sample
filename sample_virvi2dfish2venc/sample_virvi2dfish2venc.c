/******************************************************************************
  Copyright (C), 2001-2017, Allwinner Tech. Co., Ltd.
 ******************************************************************************
  File Name     : sample_virvi2dfish2venc.c
  Version       : Initial Draft
  Author        : Allwinner BU3-PD2 Team
  Created       : 2017/1/5
  Last Modified :
  Description   : mpp component implement
  Function List :
  History       :
******************************************************************************/

#define LOG_TAG "sample_virvi2dfish2venc"
#include <utils/plat_log.h>
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

#include "media/mpi_sys.h"
#include "media/mpi_vi.h"
#include "media/mpi_ise.h"
#include "media/mpi_venc.h"
#include "media/mpi_isp.h"

#include <confparser.h>
#include "sample_virvi2dfish2venc.h"
#include "sample_virvi2dfish2venc_config.h"

typedef struct awVI_pCap_S {
    VI_DEV VI_Dev;
    VI_CHN VI_Chn;
    MPP_CHN_S VI_CHN_S;
    AW_S32 s32MilliSec;
    VIDEO_FRAME_INFO_S pstFrameInfo;
    VI_ATTR_S stAttr;
} VIRVI_Cap_S;

typedef struct awISE_PortCap_S {
    MPP_CHN_S ISE_Port_S;
    FILE *OutputFilePath;
    ISE_CHN_ATTR_S PortAttr;
} ISE_PortCap_S;

typedef struct awISE_pGroupCap_S {
    ISE_GRP ISE_Group;
    MPP_CHN_S ISE_Group_S;
    ISE_GROUP_ATTR_S pGrpAttr;
    ISE_CHN ISE_Port[4];
    ISE_PortCap_S *PortCap_S[4];
} ISE_GroupCap_S;

typedef struct awVENC_pCap_S {
    pthread_t thid;
    VENC_CHN Venc_Chn;
    MPP_CHN_S VENC_CHN_S;
    int EncoderCount;
    FILE *OutputFilePath;
    VENC_STREAM_S VencFrame;
    VENC_CHN_ATTR_S mVEncChnAttr;
    VENC_FRAME_RATE_S stFrameRate;
    VencHeaderData vencheader;
} VENC_Cap_S;

/*MPI VI*/
int aw_vipp_creat(VI_DEV ViDev, VI_ATTR_S *pstAttr)
{
    int ret = -1;
    ret = AW_MPI_VI_CreateVipp(ViDev);
    if(ret < 0) {
        aloge("Create vi dev[%d] falied!", ViDev);
        return ret;
    }
    ret = AW_MPI_VI_SetVippAttr(ViDev, pstAttr);
    if(ret < 0) {
        aloge("Set vi attr[%d] falied!", ViDev);
        return ret;
    }
    ret = AW_MPI_VI_EnableVipp(ViDev);
    if(ret < 0) {
        aloge("Enable vi dev[%d] falied!", ViDev);
        return ret;
    }
    AW_MPI_ISP_Init();
    if(ViDev == 0 || ViDev == 2)
        AW_MPI_ISP_Run(1); // 3A ini
    if(ViDev == 1 || ViDev == 3)
        AW_MPI_ISP_Run(0); // 3A ini
    return 0;
}

int aw_vipp_destory(VI_DEV ViDev)
{
    int ret = -1;
    ret = AW_MPI_VI_DisableVipp(ViDev);
    if(ret < 0) {
        aloge("Disable vi dev[%d] falied!", ViDev);
        return ret;
    }
    ret = AW_MPI_VI_DestoryVipp(ViDev);
    if(ret < 0) {
        aloge("Destory vi dev[%d] falied!", ViDev);
        return ret;
    }
    return 0;
}

int aw_virvi_creat(VI_DEV ViDev, VI_CHN ViCh, void *pAttr)
{
    int ret = -1;
    ret = AW_MPI_VI_CreateVirChn(ViDev, ViCh, pAttr);
    if(ret < 0) {
        aloge("Create VI Chn failed,VIDev = %d,VIChn = %d",ViDev,ViCh);
        return ret ;
    }
    ret = AW_MPI_VI_SetVirChnAttr(ViDev, ViCh, pAttr);
    if(ret < 0) {
        aloge("Set VI ChnAttr failed,VIDev = %d,VIChn = %d",ViDev,ViCh);
        return ret ;
    }
    return 0;
}

int aw_virvi_destory(VI_DEV ViDev, VI_CHN ViCh)
{
    int ret = -1;
    ret = AW_MPI_VI_DestoryVirChn(ViDev, ViCh);
    if(ret < 0) {
        aloge("Destory VI Chn failed,VIDev = %d,VIChn = %d",ViDev,ViCh);
        return ret ;
    }
    return 0;
}

/*MPI ISE*/
int aw_iseport_creat(ISE_GRP IseGrp, ISE_CHN IsePort, ISE_CHN_ATTR_S *PortAttr)
{
    int ret = -1;
    ret = AW_MPI_ISE_CreatePort(IseGrp,IsePort,PortAttr);
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

/*MPI VENC*/
int aw_venc_chn_creat(VENC_Cap_S* pVENCCap)
{
    int ret = -1;
    ret = AW_MPI_VENC_CreateChn(pVENCCap->Venc_Chn, &pVENCCap->mVEncChnAttr);
    if(ret < 0) {
        aloge("Create venc channel[%d] falied!", pVENCCap->Venc_Chn);
        return ret ;
    }
    ret = AW_MPI_VENC_SetFrameRate(pVENCCap->Venc_Chn, &pVENCCap->stFrameRate);
    if(ret < 0) {
        aloge("Set venc channel[%d] Frame Rate falied!", pVENCCap->Venc_Chn);
        return ret ;
    }
    return ret;
}

int aw_venc_chn_destory(VENC_Cap_S* pVENCCap)
{
    int ret = -1;
    ret = AW_MPI_VENC_ResetChn(pVENCCap->Venc_Chn);
    if (ret < 0) {
        aloge("VENC Chn%d Reset error!",pVENCCap->Venc_Chn);
        return ret ;
    }
    ret = AW_MPI_VENC_DestroyChn(pVENCCap->Venc_Chn);
    if (ret < 0) {
        aloge("VENC Chn%d Destroy error!",pVENCCap->Venc_Chn);
        return ret ;
    }
    return ret;
}

static int ParseCmdLine(int argc, char **argv, SampleVirvi2Dfish2VencCmdLineParam *pCmdLinePara)
{
    alogd("sample_virvi2dfish2venc path:[%s], arg number is [%d]", argv[0], argc);
    int ret = 0;
    int i=1;
    memset(pCmdLinePara, 0, sizeof(SampleVirvi2Dfish2VencCmdLineParam));
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
                  "\t-path /home/sample_virvi2dfish2venc.conf\n");
            ret = 1;
            break;
        } else {
            alogd("ignore invalid CmdLine param:[%s], type -h to get how to set parameter!", argv[i]);
        }
        i++;
    }
    return ret;
}

static ERRORTYPE loadSampleVirvi2Dfish2VencConfig(SampleVirvi2Dfish2VencConfig *pConfig, const char *conf_path)
{
    int ret;
    char *ptr = NULL;
    char *pStrPixelFormat = NULL,*EncoderType = NULL;
    int VencChnNum = 0,i = 0,ISEPortNum = 0;
    CONFPARSER_S stConfParser;
    char name[256];
    ret = createConfParser(conf_path, &stConfParser);
    if(ret < 0) {
        aloge("load conf fail");
        return FAILURE;
    }
    memset(pConfig, 0, sizeof(SampleVirvi2Dfish2VencConfig));
    pConfig->AutoTestCount = GetConfParaInt(&stConfParser, SAMPLE_Virvi2Dfish2Venc_Auto_Test_Count, 0);

    /* Get VI parameter*/
    pConfig->VIDevConfig.SrcFrameRate = GetConfParaInt(&stConfParser, SAMPLE_Virvi2Dfish2Venc_Src_Frame_Rate, 0);
    pConfig->VIDevConfig.SrcWidth = GetConfParaInt(&stConfParser, SAMPLE_Virvi2Dfish2Venc_Src_Width, 0);
    pConfig->VIDevConfig.SrcHeight = GetConfParaInt(&stConfParser, SAMPLE_Virvi2Dfish2Venc_Src_Height, 0);
    if(pConfig->VIDevConfig.SrcWidth % 32 != 0 || pConfig->VIDevConfig.SrcHeight % 32 != 0) {
        aloge("fatal error! vi src width and src height must multiple of 32,width = %d,height = %d\n",
              pConfig->VIDevConfig.SrcWidth,pConfig->VIDevConfig.SrcHeight);
        return FAILURE;
    }
    printf("VI Parameter:src_width = %d, src_height = %d, src_frame_rate = %d\n",
           pConfig->VIDevConfig.SrcWidth,pConfig->VIDevConfig.SrcHeight,pConfig->VIDevConfig.SrcFrameRate);

    /* Get ISE parameter*/
    pConfig->ISEGroupConfig.ISEPortNum = GetConfParaInt(&stConfParser, SAMPLE_Virvi2Dfish2Venc_ISE_Port_Num, 0);
    ISEPortNum = pConfig->ISEGroupConfig.ISEPortNum;
    pConfig->ISEGroupConfig.Lens_Parameter_P0 = pConfig->VIDevConfig.SrcWidth/3.1415;
    pConfig->ISEGroupConfig.Lens_Parameter_Cx0 = pConfig->VIDevConfig.SrcWidth/2;
    pConfig->ISEGroupConfig.Lens_Parameter_Cy0 = pConfig->VIDevConfig.SrcHeight/2;
    pConfig->ISEGroupConfig.Lens_Parameter_P1 = pConfig->VIDevConfig.SrcWidth/3.1415;
    pConfig->ISEGroupConfig.Lens_Parameter_Cx1 = pConfig->VIDevConfig.SrcWidth/2;
    pConfig->ISEGroupConfig.Lens_Parameter_Cy1 = pConfig->VIDevConfig.SrcHeight/2;
    printf("ISE Group Parameter:port_num = %d,Lens_Parameter_p0 = %f,Lens_Parameter_cx0 = %d,Lens_Parameter_cy0 = %d,"
           "Lens_Parameter_p1 = %f,Lens_Parameter_cx1 = %d,Lens_Parameter_cy1 = %d,\n",pConfig->ISEGroupConfig.ISEPortNum,
           pConfig->ISEGroupConfig.Lens_Parameter_P0,pConfig->ISEGroupConfig.Lens_Parameter_Cx0,
           pConfig->ISEGroupConfig.Lens_Parameter_Cy0,pConfig->ISEGroupConfig.Lens_Parameter_P1,
           pConfig->ISEGroupConfig.Lens_Parameter_Cx1,pConfig->ISEGroupConfig.Lens_Parameter_Cy1);
    float calib_matr[3][3] = {{1,0,0},{0,1,0},{0,0,1}};
    for(int ii = 0; ii < 3; ii++) {
        for(int jj = 0; jj < 3; jj++) {
            pConfig->ISEGroupConfig.calib_matr[ii][jj] = calib_matr[ii][jj];
        }
    }
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
        printf("ISE Port%d Parameter:ISE_Width = %d,ISE_Height = %d,flip_enable = %d,mirror_enable = %d\n",
               i,pConfig->ISEPortConfig[i].ISEWidth,pConfig->ISEPortConfig[i].ISEHeight,
               pConfig->ISEPortConfig[i].flip_enable,pConfig->ISEPortConfig[i].mirror_enable);
    }
    /* Get Venc parameter*/
    pConfig->VencChnNum = GetConfParaInt(&stConfParser, SAMPLE_Virvi2Dfish2Venc_Venc_Chn_Num, 0);
    VencChnNum = pConfig->VencChnNum;
    if(ISEPortNum != VencChnNum) {
        aloge("fatal error! VencChnNum not equal to ISEPortNum!!");
        ret = -1;
        return ret;
    }
    EncoderType =(char*) GetConfParaString(&stConfParser, SAMPLE_Virvi2Dfish2Venc_Venc_Encoder_Type, NULL);
    if(!strcmp(EncoderType, "H.264")) {
        pConfig->EncoderType = PT_H264;
    } else if(!strcmp(EncoderType, "H.265")) {
        pConfig->EncoderType = PT_H265;
    } else if(!strcmp(EncoderType, "MJPEG")) {
        pConfig->EncoderType = PT_MJPEG;
    } else {
        alogw("unsupported venc type:%p,encoder type turn to H.264!",pConfig->EncoderType);
        pConfig->EncoderType = PT_H264;
    }
    pStrPixelFormat = (char*)GetConfParaString(&stConfParser, SAMPLE_Virvi2Dfish2Venc_Venc_Picture_Format, NULL);
    if(!strcmp(pStrPixelFormat, "nv21")) {
        pConfig->DestPicFormat = MM_PIXEL_FORMAT_YVU_SEMIPLANAR_420;
    } else {
        aloge("fatal error! conf file pic_format must be yuv420sp");
        pConfig->DestPicFormat = MM_PIXEL_FORMAT_YVU_SEMIPLANAR_420;
    }
    pConfig->EncoderCount = GetConfParaInt(&stConfParser, SAMPLE_Virvi2Dfish2Venc_Venc_Encoder_Count, 0);
    printf("Venc Parameter:VencChnNum = %d,encoder_type = %d,"
           "encoder_count = %d,dest_picture_format = %d\n",
           VencChnNum,pConfig->EncoderType,pConfig->EncoderCount,pConfig->DestPicFormat);
    for(i = 0; i < VencChnNum; i++) {
        snprintf(name, 256, "venc_chn%d_dest_width", i);
        pConfig->VencChnConfig[i].DestWidth = GetConfParaInt(&stConfParser, name, 0);
        snprintf(name, 256, "venc_chn%d_dest_height", i);
        pConfig->VencChnConfig[i].DestHeight = GetConfParaInt(&stConfParser, name, 0);
        snprintf(name, 256, "venc_chn%d_dest_frame_rate", i);
        pConfig->VencChnConfig[i].DestFrameRate = GetConfParaInt(&stConfParser, name, 0);
        snprintf(name, 256, "venc_chn%d_dest_bit_rate", i);
        pConfig->VencChnConfig[i].DestBitRate = GetConfParaInt(&stConfParser, name, 0);
        snprintf(name, 256, "venc_chn%d_output_file_path", i);
        ptr = (char*)GetConfParaString(&stConfParser, name, NULL);
        strncpy(pConfig->VencChnConfig[i].OutputFilePath, ptr, MAX_FILE_PATH_SIZE-1);
        pConfig->VencChnConfig[i].OutputFilePath[MAX_FILE_PATH_SIZE-1] = '\0';
        printf("VENC Chn%d Parameter:output_file_path = %s\n",
               i,pConfig->VencChnConfig[i].OutputFilePath);
        printf("dest_width = %d, dest_height = %d, dest_frame_rate = %d, dest_bit_rate = %d\n",
               pConfig->VencChnConfig[i].DestWidth,pConfig->VencChnConfig[i].DestHeight,
               pConfig->VencChnConfig[i].DestFrameRate,pConfig->VencChnConfig[i].DestBitRate);
    }

    destroyConfParser(&stConfParser);
    return SUCCESS;
}

static void *GetVencImage_Thread(void *pArg)
{
    int ret = 0;
    int i = 0;
    VENC_Cap_S *pCap = (VENC_Cap_S *)pArg;
    VENC_STREAM_S VencFrame;
    VENC_PACK_S venc_pack;
    VencFrame.mPackCount = 1;
    VencFrame.mpPack = &venc_pack;
    while((i != pCap->EncoderCount) || (pCap->EncoderCount == -1)) {
        if((ret = AW_MPI_VENC_GetStream(pCap->Venc_Chn,&VencFrame,4000)) < 0) { //6000(25fps) 4000(30fps)
            aloge("venc chn%d get stream failed!",pCap->Venc_Chn);
            continue;
        } else {
            if(VencFrame.mpPack->mpAddr0 != NULL && VencFrame.mpPack->mLen0) {
                fwrite(VencFrame.mpPack->mpAddr0,1,VencFrame.mpPack->mLen0,pCap->OutputFilePath);
            }
            if(VencFrame.mpPack->mpAddr1 != NULL && VencFrame.mpPack->mLen1) {
                fwrite(VencFrame.mpPack->mpAddr1,1,VencFrame.mpPack->mLen1,pCap->OutputFilePath);
            }
            ret = AW_MPI_VENC_ReleaseStream(pCap->Venc_Chn,&VencFrame);
            if(ret < 0) { //108000
                aloge("venc chn%d release stream failed!",pCap->Venc_Chn);
            }
        }
        i++;
    }
    return NULL;
}

void Virvi2Dfish2Venc_HELP()
{
    printf("Run CSI0/CSI1+ISE+Venc command: ./sample_virvi2dfish2venc -path ./sample_virvi2dfish2venc.conf\r\n");
}

int main(int argc, char *argv[])
{
    int ret = -1, i = 0, j = 0,count = 0;
    int result = 0;
    int AutoTestCount = 0;
    printf("sample_virvi2dfish2venc build time = %s, %s.\r\n", __DATE__, __TIME__);
    if (argc != 3) {
        Virvi2Dfish2Venc_HELP();
        exit(0);
    }

    VIRVI_Cap_S    pVICap[MAX_VIR_CHN_NUM];
    ISE_PortCap_S  pISEPortCap[ISE_MAX_CHN_NUM];
    ISE_GroupCap_S pISEGroupCap[ISE_MAX_GRP_NUM];
    VENC_Cap_S     pVENCCap[VENC_MAX_CHN_NUM];

    SampleVirvi2Dfish2VencConfparser stContext;
    //parse command line param,read sample_virvi2dfish2venc.conf
    if(ParseCmdLine(argc, argv, &stContext.mCmdLinePara) != 0) {
        aloge("fatal error! command line param is wrong, exit!");
        result = -1;
        goto _exit;
    }
    char *pConfigFilePath;
    if(strlen(stContext.mCmdLinePara.mConfigFilePath) > 0) {
        pConfigFilePath = stContext.mCmdLinePara.mConfigFilePath;
    } else {
        pConfigFilePath = DEFAULT_SAMPLE_VIRVI2DFISH2VENC_CONF_PATH;
    }
    //parse config file.
    if(loadSampleVirvi2Dfish2VencConfig(&stContext.mConfigPara, pConfigFilePath) != SUCCESS) {
        aloge("fatal error! no config file or parse conf file fail");
        result = -1;
        goto _exit;
    }
    AutoTestCount = stContext.mConfigPara.AutoTestCount;
    while(count != AutoTestCount) {
        /*Set VI Channel Attribute*/
        for(i = 0; i < 2; i++) {
            memset(&pVICap[i], 0, sizeof(VIRVI_Cap_S));
            pVICap[i].stAttr.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
            pVICap[i].stAttr.memtype = V4L2_MEMORY_MMAP;
            pVICap[i].stAttr.format.pixelformat = V4L2_PIX_FMT_NV21M;
            pVICap[i].stAttr.format.colorspace = V4L2_COLORSPACE_JPEG;
            pVICap[i].stAttr.format.field = V4L2_FIELD_NONE;
            pVICap[i].stAttr.format.width = stContext.mConfigPara.VIDevConfig.SrcWidth;
            pVICap[i].stAttr.format.height = stContext.mConfigPara.VIDevConfig.SrcHeight;
            pVICap[i].stAttr.fps = stContext.mConfigPara.VIDevConfig.SrcFrameRate;
            pVICap[i].stAttr.nbufs = 5;
            pVICap[i].stAttr.nplanes = 2;
            pVICap[i].s32MilliSec = 5000;
            pVICap[i].VI_Chn = 0;
            pVICap[i].VI_Dev = i;
            pVICap[i].VI_CHN_S.mDevId = i;
            pVICap[i].VI_CHN_S.mChnId = 0;
            pVICap[i].VI_CHN_S.mModId = MOD_ID_VIU;
        }

        int ISEPortNum = 0;
        ISEPortNum = stContext.mConfigPara.ISEGroupConfig.ISEPortNum;
        memset(&pISEGroupCap[0], 0, sizeof(ISE_GroupCap_S));
        for(j = 0; j < 1; j++) {
            /*Set ISE Group Attribute*/
            pISEGroupCap[j].ISE_Group = j;  //Group ID
            pISEGroupCap[j].pGrpAttr.iseMode = ISEMODE_TWO_FISHEYE;
            /*ise group bind channel attr*/
            pISEGroupCap[j].ISE_Group_S.mChnId = 0;
            pISEGroupCap[j].ISE_Group_S.mDevId = j;
            pISEGroupCap[j].ISE_Group_S.mModId = MOD_ID_ISE;
            for(i = 0; i < ISEPortNum; i++) {
                memset(&pISEPortCap[i], 0, sizeof(ISE_PortCap_S));
                pISEGroupCap[j].ISE_Port[i] = i;  //Port ID
                /*ise port bind channel attr*/
                pISEPortCap[i].ISE_Port_S.mModId = MOD_ID_ISE;
                pISEPortCap[i].ISE_Port_S.mDevId = j;
                pISEPortCap[i].ISE_Port_S.mChnId = i;
                /*Set ISE Port Attribute*/
                if(i == 0) { //dfish arttr
                    pISEPortCap[i].PortAttr.mode_attr.mDFish.ise_cfg.in_w = stContext.mConfigPara.VIDevConfig.SrcWidth;
                    pISEPortCap[i].PortAttr.mode_attr.mDFish.ise_cfg.in_h = stContext.mConfigPara.VIDevConfig.SrcHeight;
                    pISEPortCap[i].PortAttr.mode_attr.mDFish.ise_cfg.in_luma_pitch =  stContext.mConfigPara.VIDevConfig.SrcWidth;
                    pISEPortCap[i].PortAttr.mode_attr.mDFish.ise_cfg.in_chroma_pitch =  stContext.mConfigPara.VIDevConfig.SrcWidth;
                    pISEPortCap[i].PortAttr.mode_attr.mDFish.ise_cfg.in_yuv_type = 0;
                    pISEPortCap[i].PortAttr.mode_attr.mDFish.ise_cfg.out_yuv_type = 0;
                    pISEPortCap[i].PortAttr.mode_attr.mDFish.ise_cfg.p0 = stContext.mConfigPara.ISEGroupConfig.Lens_Parameter_P0;
                    pISEPortCap[i].PortAttr.mode_attr.mDFish.ise_cfg.cx0 = stContext.mConfigPara.ISEGroupConfig.Lens_Parameter_Cx0;
                    pISEPortCap[i].PortAttr.mode_attr.mDFish.ise_cfg.cy0 = stContext.mConfigPara.ISEGroupConfig.Lens_Parameter_Cy0;
                    pISEPortCap[i].PortAttr.mode_attr.mDFish.ise_cfg.p1 = stContext.mConfigPara.ISEGroupConfig.Lens_Parameter_P1;
                    pISEPortCap[i].PortAttr.mode_attr.mDFish.ise_cfg.cx1 = stContext.mConfigPara.ISEGroupConfig.Lens_Parameter_Cx1;
                    pISEPortCap[i].PortAttr.mode_attr.mDFish.ise_cfg.cy1 = stContext.mConfigPara.ISEGroupConfig.Lens_Parameter_Cy1;
                    for(int ii = 0; ii < 3; ii++) {
                        for(int jj = 0; jj < 3; jj++) {
                            pISEPortCap->PortAttr.mode_attr.mDFish.ise_cfg.calib_matr[ii][jj] = stContext.mConfigPara.ISEGroupConfig.calib_matr[ii][jj];
                        }
                    }
                }
                pISEPortCap[i].PortAttr.mode_attr.mDFish.ise_cfg.out_en[i] = 1;
                pISEPortCap[i].PortAttr.mode_attr.mDFish.ise_cfg.out_w[i] =  stContext.mConfigPara.ISEPortConfig[i].ISEWidth;
                pISEPortCap[i].PortAttr.mode_attr.mDFish.ise_cfg.out_h[i] =  stContext.mConfigPara.ISEPortConfig[i].ISEHeight;
                pISEPortCap[i].PortAttr.mode_attr.mDFish.ise_cfg.out_flip[i] =  stContext.mConfigPara.ISEPortConfig[i].flip_enable;
                pISEPortCap[i].PortAttr.mode_attr.mDFish.ise_cfg.out_mirror[i] =  stContext.mConfigPara.ISEPortConfig[i].mirror_enable;
                pISEPortCap[i].PortAttr.mode_attr.mDFish.ise_cfg.out_luma_pitch[i] =  stContext.mConfigPara.ISEPortConfig[i].ISEStride;
                pISEPortCap[i].PortAttr.mode_attr.mDFish.ise_cfg.out_chroma_pitch[i] =  stContext.mConfigPara.ISEPortConfig[i].ISEStride;;
                pISEGroupCap[j].PortCap_S[i] = &pISEPortCap[i];
            }
        }

        /*Set VENC Channel Attribute*/
        int VencChnNum = 0;
        VencChnNum = stContext.mConfigPara.VencChnNum;
        int VIFrameRate = stContext.mConfigPara.VIDevConfig.SrcFrameRate;
        int VencFrameRate = 0;
        int wantedFrameRate = 0,wantedVideoBitRate = 0;
        PAYLOAD_TYPE_E videoCodec = stContext.mConfigPara.EncoderType;
        PIXEL_FORMAT_E wantedPreviewFormat = stContext.mConfigPara.DestPicFormat;
        for(i = 0; i < VencChnNum; i++) {
            memset(&pVENCCap[i], 0, sizeof(VENC_Cap_S));
            SIZE_S videosize = {stContext.mConfigPara.ISEPortConfig[i].ISEWidth, stContext.mConfigPara.ISEPortConfig[i].ISEHeight};
            VencFrameRate = stContext.mConfigPara.VencChnConfig[i].DestFrameRate;
            SIZE_S wantedVideoSize = {stContext.mConfigPara.VencChnConfig[i].DestWidth, stContext.mConfigPara.VencChnConfig[i].DestHeight};
            wantedFrameRate = stContext.mConfigPara.VencChnConfig[i].DestFrameRate;
            wantedVideoBitRate = stContext.mConfigPara.VencChnConfig[i].DestBitRate;

            pVENCCap[i].thid = 0;
            pVENCCap[i].Venc_Chn = i;
            pVENCCap[i].mVEncChnAttr.VeAttr.Type = videoCodec;
            pVENCCap[i].mVEncChnAttr.VeAttr.SrcPicWidth = videosize.Width;
            pVENCCap[i].mVEncChnAttr.VeAttr.SrcPicHeight = videosize.Height;
            pVENCCap[i].mVEncChnAttr.VeAttr.Field = VIDEO_FIELD_FRAME;
            pVENCCap[i].mVEncChnAttr.VeAttr.PixelFormat = wantedPreviewFormat;
            pVENCCap[i].stFrameRate.SrcFrmRate = VIFrameRate;
            pVENCCap[i].stFrameRate.DstFrmRate = VencFrameRate;
            pVENCCap[i].EncoderCount = stContext.mConfigPara.EncoderCount;
            /*venc chn bind channel attr*/
            pVENCCap[i].VENC_CHN_S.mChnId = i;
            pVENCCap[i].VENC_CHN_S.mDevId = 0;
            pVENCCap[i].VENC_CHN_S.mModId = MOD_ID_VENC;
            if(PT_H264 == pVENCCap[i].mVEncChnAttr.VeAttr.Type) {
                pVENCCap[i].mVEncChnAttr.VeAttr.AttrH264e.bByFrame = TRUE;
                pVENCCap[i].mVEncChnAttr.VeAttr.AttrH264e.Profile = 2;
                pVENCCap[i].mVEncChnAttr.VeAttr.AttrH264e.PicWidth = wantedVideoSize.Width;
                pVENCCap[i].mVEncChnAttr.VeAttr.AttrH264e.PicHeight = wantedVideoSize.Height;
                pVENCCap[i].mVEncChnAttr.RcAttr.mRcMode = VENC_RC_MODE_H264CBR;
                pVENCCap[i].mVEncChnAttr.RcAttr.mAttrH264Cbr.mSrcFrmRate = wantedFrameRate;
                if(stContext.mConfigPara.VencChnConfig[0].mTimeLapseEnable) {
                    pVENCCap[i].mVEncChnAttr.RcAttr.mAttrH264Cbr.fr32DstFrmRate = 1000 + (stContext.mConfigPara.VencChnConfig[0].mTimeBetweenFrameCapture<<16);
                } else {
                    pVENCCap[i].mVEncChnAttr.RcAttr.mAttrH264Cbr.fr32DstFrmRate = wantedFrameRate;
                }
                pVENCCap[i].mVEncChnAttr.RcAttr.mAttrH264Cbr.mBitRate = wantedVideoBitRate;
            } else if(PT_H265 == pVENCCap[i].mVEncChnAttr.VeAttr.Type) {
                pVENCCap[i].mVEncChnAttr.VeAttr.AttrH265e.mbByFrame = TRUE;
                pVENCCap[i].mVEncChnAttr.VeAttr.AttrH265e.mProfile = 2;
                pVENCCap[i].mVEncChnAttr.VeAttr.AttrH265e.mPicWidth = wantedVideoSize.Width;
                pVENCCap[i].mVEncChnAttr.VeAttr.AttrH265e.mPicHeight = wantedVideoSize.Height;
                pVENCCap[i].mVEncChnAttr.RcAttr.mRcMode = VENC_RC_MODE_H265CBR;
                pVENCCap[i].mVEncChnAttr.RcAttr.mAttrH265Cbr.mSrcFrmRate = wantedFrameRate;
                pVENCCap[i].mVEncChnAttr.RcAttr.mAttrH265Cbr.fr32DstFrmRate = wantedFrameRate;
                pVENCCap[i].mVEncChnAttr.RcAttr.mAttrH265Cbr.mBitRate = wantedVideoBitRate;
            } else if(PT_MJPEG == pVENCCap[i].mVEncChnAttr.VeAttr.Type) {
                pVENCCap[i].mVEncChnAttr.VeAttr.AttrMjpeg.mbByFrame = TRUE;
                pVENCCap[i].mVEncChnAttr.VeAttr.AttrMjpeg.mPicWidth= videosize.Width;
                pVENCCap[i].mVEncChnAttr.VeAttr.AttrMjpeg.mPicHeight = videosize.Height;
                pVENCCap[i].mVEncChnAttr.RcAttr.mRcMode = VENC_RC_MODE_MJPEGCBR;
                pVENCCap[i].mVEncChnAttr.RcAttr.mAttrMjpegeCbr.mBitRate = wantedVideoBitRate;
            }
            pVENCCap[i].OutputFilePath = fopen(stContext.mConfigPara.VencChnConfig[i].OutputFilePath, "wb+");
            if(!pVENCCap[i].OutputFilePath ) {
                aloge("fatal error! can't open encodervideo file[%s]",
                      stContext.mConfigPara.VencChnConfig[i].OutputFilePath);
                result = -1;
                goto _exit;
            }
        }

        /* start mpp systerm */
        MPP_SYS_CONF_S mSysConf;
        memset(&mSysConf,0,sizeof(MPP_SYS_CONF_S));
        mSysConf.nAlignWidth = 32;
        AW_MPI_SYS_SetConf(&mSysConf);
        ret = AW_MPI_SYS_Init();
        if (ret < 0) {
            aloge("sys Init failed!");
            result = -1;
            goto sys_exit;
        }

        /* creat vi component */
        for (i = 0; i < 2; i++) {
            ret = aw_vipp_creat(pVICap[i].VI_Dev, &pVICap[i].stAttr);
            if(ret < 0) {
                aloge("vipp creat failed,VIDev = %d",pVICap[i].VI_Dev);
                result = -1;
                goto vipp_exit;
            }
            ret = aw_virvi_creat(pVICap[i].VI_Dev,pVICap[i].VI_Chn, NULL);
            if(ret < 0) {
                aloge("virvi creat failed,VIDev = %d,VIChn = %d",pVICap[i].VI_Dev,pVICap[i].VI_Chn);
                result = -1;
                goto virvi_exit;
            }
        }

        /* creat ise component */
        ret = aw_isegroup_creat(pISEGroupCap[0].ISE_Group, &pISEGroupCap[0].pGrpAttr);
        if(ret < 0) {
            aloge("ISE Group %d creat failed",pISEGroupCap[0].ISE_Group);
            result = -1;
            goto ise_group_exit;
        }
        for(i = 0; i < ISEPortNum; i++) {
            ret = aw_iseport_creat(pISEGroupCap[0].ISE_Group,pISEGroupCap[0].ISE_Port[i],&(pISEGroupCap[0].PortCap_S[i]->PortAttr));
            if(ret < 0) {
                aloge("ISE Port%d creat failed",pISEGroupCap[0].ISE_Port[i]);
                result = -1;
                goto ise_port_exit;
            }
        }

        /* creat venc component */
        for(i = 0; i < VencChnNum; i++) {
            ret = aw_venc_chn_creat(&pVENCCap[i]);
            if(ret < 0) {
                aloge("Venc Chn%d Creat failed!",pVENCCap[i].Venc_Chn);
                result = -1;
                goto venc_chn_exit;
            }
        }
        for(i = 0; i < VencChnNum; i++) {
            if(PT_H264 == pVENCCap[i].mVEncChnAttr.VeAttr.Type) {
                AW_MPI_VENC_GetH264SpsPpsInfo(pVENCCap[i].Venc_Chn, &pVENCCap[i].vencheader);
                if(pVENCCap[i].vencheader.nLength) {
                    fwrite(pVENCCap[i].vencheader.pBuffer,pVENCCap[i].vencheader.nLength,1,pVENCCap[i].OutputFilePath);
                }
            } else if(PT_H265 == pVENCCap[i].mVEncChnAttr.VeAttr.Type) {
                AW_MPI_VENC_GetH265SpsPpsInfo(pVENCCap[i].Venc_Chn, &pVENCCap[i].vencheader);
                if(pVENCCap[i].vencheader.nLength) {
                    fwrite(pVENCCap[i].vencheader.pBuffer,pVENCCap[i].vencheader.nLength,1,pVENCCap[i].OutputFilePath);
                }
            }
        }
        /* bind component */
        for(i = 0; i < 2; i++) {
            if (pVICap[i].VI_CHN_S.mDevId >= 0  && pISEGroupCap[0].ISE_Group_S.mDevId >= 0) {
                ret = AW_MPI_SYS_Bind(&pVICap[i].VI_CHN_S,&pISEGroupCap[0].ISE_Group_S);
                if(ret !=SUCCESS) {
                    aloge("error!!! VI dev%d can not bind ISE Group%d!!!\n",
                          pVICap[i].VI_CHN_S.mDevId,pISEGroupCap[0].ISE_Group_S.mDevId);
                    result = -1;
                    goto vi_bind_ise_exit;
                }
            }
        }
        for(i = 0; i < ISEPortNum; i++) {
            if (pISEGroupCap[0].PortCap_S[i]->ISE_Port_S.mChnId >= 0 && pVENCCap[i].VENC_CHN_S.mChnId >= 0) {
                ret = AW_MPI_SYS_Bind(&pISEGroupCap[0].PortCap_S[i]->ISE_Port_S,&pVENCCap[i].VENC_CHN_S);
                if(ret !=SUCCESS) {
                    aloge("error!!! ISE Port%d can not bind VencChn%d!!!\n",
                          pISEGroupCap[0].PortCap_S[i]->ISE_Port_S.mChnId,pVENCCap[i].VENC_CHN_S.mChnId);
                    result = -1;
                    goto ise_bind_venc_exit;
                }
            }
        }

        /* start component */
        for (i = 0; i < 2; i++) {
            ret = AW_MPI_VI_EnableVirChn(pVICap[i].VI_Dev, pVICap[i].VI_Chn);
            if (ret < 0) {
                aloge("VI enable error! VIDev = %d,VIChn = %d",pVICap[i].VI_Dev, pVICap[i].VI_Chn);
                result = -1;
                goto vi_stop;
            }
        }
        ret = AW_MPI_ISE_Start(pISEGroupCap[0].ISE_Group);
        if (ret < 0) {
            aloge("ISE Start error!");
            result = -1;
            goto ise_stop;
        }
        for(i = 0; i < VencChnNum; i++) {
            ret = AW_MPI_VENC_StartRecvPic(pVENCCap[i].Venc_Chn);
            if (ret < 0) {
                aloge("VENC Chn%d Start RecvPic error!",pVENCCap[i].Venc_Chn);
                result = -1;
                goto venc_stop;
            }
        }

        for (i = 0; i < VencChnNum; i++) {
            ret = pthread_create(&pVENCCap[i].thid, NULL, GetVencImage_Thread, &pVENCCap[i]);
        }
        for (i = 0; i < VencChnNum; i++) {
            pthread_join(pVENCCap[i].thid, NULL);
        }

        for (i = 0; i < 2; i++) {
            if(pVICap[i].VI_Dev == 0 || pVICap[i].VI_Dev == 2)
                AW_MPI_ISP_Stop(1);
            if(pVICap[i].VI_Dev == 1 || pVICap[i].VI_Dev == 3)
                AW_MPI_ISP_Stop(0);
            AW_MPI_ISP_Exit();
        }

        /* stop component */
venc_stop:
        for(i = 0; i < VencChnNum; i++) {
            ret = AW_MPI_VENC_StopRecvPic(pVENCCap[i].Venc_Chn);
            if (ret < 0) {
                aloge("VENC Chn%d Stop Receive Picture error!",pVENCCap[i].Venc_Chn);
                result = -1;
                goto _exit;
            }
        }

ise_stop:
        ret = AW_MPI_ISE_Stop(pISEGroupCap[0].ISE_Group);
        if (ret < 0) {
            aloge("ISE Stop error!");
            result = -1;
            goto _exit;
        }

vi_stop:
        for (i = 0; i < 2; i++) {
            ret = AW_MPI_VI_DisableVirChn(pVICap[i].VI_Dev, pVICap[i].VI_Chn);
            if(ret < 0) {
                aloge("Disable VI Chn failed,VIDev = %d,VIChn = %d",pVICap[i].VI_Dev, pVICap[i].VI_Chn);
                result = -1;
                goto _exit;
            }
        }

vi_bind_ise_exit:
        for(i = 0; i < 2; i++) {
            if (pVICap[i].VI_CHN_S.mDevId >= 0  && pISEGroupCap[0].ISE_Group_S.mDevId >= 0) {
                ret = AW_MPI_SYS_UnBind(&pVICap[i].VI_CHN_S,&pISEGroupCap[0].ISE_Group_S);
                if(ret !=SUCCESS) {
                    aloge("error!!! VI dev%d can not bind ISE Group%d!!!\n",
                          pVICap[i].VI_CHN_S.mDevId,pISEGroupCap[0].ISE_Group_S.mDevId);
                    result = -1;
                    goto _exit;
                }
            }
        }

ise_bind_venc_exit:
        for(i = 0; i < ISEPortNum; i++) {
            if (pISEGroupCap[0].PortCap_S[i]->ISE_Port_S.mChnId >= 0 && pVENCCap[i].VENC_CHN_S.mChnId >= 0) {
                ret = AW_MPI_SYS_UnBind(&pISEGroupCap[0].PortCap_S[i]->ISE_Port_S,&pVENCCap[i].VENC_CHN_S);
                if(ret !=SUCCESS) {
                    aloge("error!!! ISE Port%d can not bind VencChn%d!!!\n",
                          pISEGroupCap[0].PortCap_S[i]->ISE_Port_S.mChnId,pVENCCap[i].VENC_CHN_S.mChnId);
                    result = -1;
                    goto _exit;
                }
            }
        }

venc_chn_exit:
        /* destory venc component */
        for(i = 0; i < VencChnNum; i++) {
            ret = aw_venc_chn_destory(&pVENCCap[i]);
            if(ret < 0) {
                aloge("Venc Chn%d distory failed!",pVENCCap[i].Venc_Chn);
                result = -1;
                goto _exit;
            }
        }

ise_port_exit:
        /* destory ise component */
        for(i = 0; i < ISEPortNum; i++) {
            ret = aw_iseport_destory(pISEGroupCap[0].ISE_Group, pISEGroupCap[0].ISE_Port[i]);
            if (ret < 0) {
                aloge("ISE Port%d Stop error!",pISEGroupCap[0].ISE_Port[i]);
                result = -1;
                goto _exit;
            }
        }

ise_group_exit:
        ret = aw_isegroup_destory(pISEGroupCap[0].ISE_Group);
        if (ret < 0) {
            aloge("ISE Destroy Group%d error!",pISEGroupCap[0].ISE_Group);
            result = -1;
            goto _exit;
        }

virvi_exit:
        /* destory vi component */
        for (i = 0; i < 2; i++) {
            ret = aw_virvi_destory(pVICap[i].VI_Dev, pVICap[i].VI_Chn);
            if (ret < 0) {
                aloge("virvi end error! VIDev = %d,VIChn = %d",
                      pVICap[i].VI_Dev, pVICap[i].VI_Chn);
                result = -1;
                goto _exit;
            }
        }
vipp_exit:
        for (i = 0; i < 2; i++) {
            ret = aw_vipp_destory(pVICap[i].VI_Dev);
            if (ret < 0) {
                aloge("vipp end error! VIDev = %d",pVICap[i].VI_Dev);
                result = -1;
                goto _exit;
            }
        }

sys_exit:
        /* exit mpp systerm */
        ret = AW_MPI_SYS_Exit();
        if (ret < 0) {
            aloge("sys exit failed!");
            result = -1;
            goto _exit;
        }
        for (i = 0; i < VencChnNum; i++) {
            fflush(pVENCCap[i].OutputFilePath);
            fclose(pVENCCap[i].OutputFilePath);
        }
        printf("======================================.\r\n");
        printf("Auto Test count end: %d. (MaxCount==1000).\r\n", count);
        printf("======================================.\r\n");
        count ++;
    }
    printf("sample_virvi2dfish2venc exit!\n");
    return 0;
_exit:
    return result;
}
