#include "config.h"
#include <string.h>
#include <iostream>
#include <sstream>
#include <fstream>
#include <map>
#include <vector>
#include <cstring>
#include <unistd.h>
#include <filesystem>
#include <variant>
#include <string>
#include <chrono>
#include <sdbusplus/timer.hpp>
#include <fcntl.h>
#include <phosphor-logging/lg2.hpp>
#include <phosphor-logging/elog-errors.hpp>
#include <phosphor-logging/elog.hpp>
#include <xyz/openbmc_project/Common/error.hpp>

#include "bert.hpp"
#include "utils.hpp"
extern "C"
{
#include <spinorfs.h>
}

PHOSPHOR_LOG2_USING;
using namespace phosphor::logging;

#undef BERT_DEBUG

#define BERT_SENSOR_TYPE_OEM         0xC1
#define BERT_EVENT_CODE_OEM          0x04

#define PROC_MTD_INFO           "/proc/mtd"
#define HOST_SPI_FLASH_MTD_NAME "hnor"

std::string bertNvp = "ras-crash";
std::string bertFileNvp = "latest.ras";
std::string bertFileNvpInfo = "latest.dump";

static void bertClaimSPITimeOutHdl(void);
static int handshakeSPI(bert_handshake_cmd val);

std::unique_ptr<phosphor::Timer> bertClaimSPITimer;
bool isMasked = false;

void bertClaimSPITimeOut()
{
    bertClaimSPITimer = std::make_unique<phosphor::Timer>(
                        [&](void) { bertClaimSPITimeOutHdl(); });

}

static void addBertSELLog(sdbusplus::bus::bus& bus, uint8_t crashIndex,
                          uint32_t sectionType, uint32_t subTypeId)
{
    /* Log SEL and Redfish */
    std::vector<uint8_t> evtData;
    std::string msg = "PLDM BERT SEL Event";
    uint8_t recordType = 0xC0;
    uint8_t evtData1, evtData2, evtData3, evtData4, evtData5, evtData6;
    /*
     * OEM IPMI SEL Recode Format for RAS event:
     * evtData1:
     *    Sensor Type: 0xC1 - Ampere OEM Sensor Type
     * evtData2:
     *    Event Code: 0x04 - BMC detects BERT valid on BMC booting
     * evtData3:
     *     crash file index
     * evtData4:
     *     Section Type
     * evtData5:
     *     Error Sub Type ID high byte
     * evtData6:
     *     Error Sub Type ID low byte
     */
    evtData1 = BERT_SENSOR_TYPE_OEM;
    evtData2 = BERT_EVENT_CODE_OEM;
    evtData3 = crashIndex;
    evtData4 = sectionType ;
    evtData5 = subTypeId >> 8;
    evtData6 = subTypeId;
    /*
     * OEM data bytes
     *    Ampere IANA: 3 bytes [0x3a 0xcd 0x00]
     *    event data: 9 bytes [evtData1 evtData2 evtData3
     *                         evtData4 evtData5 evtData6
     *                         0x00     0x00     0x00 ]
     *    sel type: 1 byte [0xC0]
     */
    evtData.reserve(12);
    evtData.push_back(0x3a);
    evtData.push_back(0xcd);
    evtData.push_back(0);
    evtData.push_back(evtData1);
    evtData.push_back(evtData2);
    evtData.push_back(evtData3);
    evtData.push_back(evtData4);
    evtData.push_back(evtData5);
    evtData.push_back(evtData6);
    evtData.push_back(0);
    evtData.push_back(0);
    evtData.push_back(0);

    crashcapture::utils::addOEMSelLog(bus, msg, evtData, recordType);
}

static int handshakeSPI(bert_handshake_cmd val)
{
    std::stringstream pidStr;
    int ret = 0;

    if (val == STOP_HS)
    {
        bertClaimSPITimer->stop();
    }

    pidStr << getpid();
    std::string hsStr = (val == START_HS) ? "start_handshake" : "stop_handshake";
    std::string cmd = std::string(HANDSHAKE_SPI_SCRIPT) + " " +
                      hsStr + " " + pidStr.str();
    ret = system(cmd.c_str());
    if (ret)
    {
        error("Cannot start/stop handshake SPI-NOR");
        return ret;
    }
    if (!ret && (val == START_HS))
    {
        bertClaimSPITimer->start(std::chrono::milliseconds(BERT_CLAIMSPI_TIMEOUT));
    }
    return ret;
}

static void bertClaimSPITimeOutHdl(void)
{
    error("Timeout {VALUE} ms for claiming SPI bus. Release it", "VALUE", BERT_CLAIMSPI_TIMEOUT);
    handshakeSPI(STOP_HS);
}

static int enableAccessHostSpiNor(void)
{
    std::stringstream pidStr;
    int ret = 0;

    pidStr << getpid();
    std::string cmd = std::string(HANDSHAKE_SPI_SCRIPT) + " lock " + pidStr.str();
    ret = system(cmd.c_str());
    if (ret)
    {
        error("Cannot lock SPI-NOR resource");
        return ret;
    }
    cmd = std::string(HANDSHAKE_SPI_SCRIPT) + " bind " + pidStr.str();
    ret = system(cmd.c_str());
    if (ret)
    {
        error("Cannot bind SPI-NOR resource");
        return ret;
    }

    return ret;
}

static int disableAccessHostSpiNor(void)
{
    std::stringstream pidStr;
     int ret = 0;

     pidStr << getpid();
     std::string cmd = std::string(HANDSHAKE_SPI_SCRIPT) + " unbind " + pidStr.str();
     ret = system(cmd.c_str());
     if (ret)
     {
         error("Cannot unbind SPI-NOR resource");
     }
     cmd = std::string(HANDSHAKE_SPI_SCRIPT) + " unlock " + pidStr.str();
     ret = system(cmd.c_str());
     if (ret)
     {
         error("Cannot unlock SPI-NOR resource");
         return ret;
     }

     return ret;
}

static int spinorfsRead(char *file, char *buff, uint32_t offset, uint32_t size)
{
    int ret;

    if (spinorfs_open(file, SPINORFS_O_RDONLY))
    {
        return -1;
    }

    ret = spinorfs_read(buff, offset, size);
    spinorfs_close();

    return ret;
}

static int spinorfsWrite(char *file, char *buff, uint32_t offset, uint32_t size)
{
    int ret;

    if (spinorfs_open(file, SPINORFS_O_WRONLY | SPINORFS_O_TRUNC))
    {
        return -1;
    }

    ret = spinorfs_write(buff, offset, size);
    spinorfs_close();

    return ret;
}

static int handshakeReadSPI(bert_host_state state,
                            char *file, char *buff,
                            uint32_t size)
{
    uint32_t j = 0;

    if (state == HOST_ON)
    {
        for (j = 0; j < size/BLOCK_SIZE; j++)
        {
            if (handshakeSPI(START_HS))
            {
                return -1;
            }
            if (spinorfsRead(file, buff + j*BLOCK_SIZE,
                             j*BLOCK_SIZE, BLOCK_SIZE) < 0)
            {
                goto exit_err;
            }
            handshakeSPI(STOP_HS);
        }
        if (handshakeSPI(START_HS))
        {
            return -1;
        }
        if (spinorfsRead(file, buff + j*BLOCK_SIZE,
                         j*BLOCK_SIZE,
                         size - j*BLOCK_SIZE) < 0)
        {
            goto exit_err;
        }
        handshakeSPI(STOP_HS);
    }
    else
    {
        if (spinorfsRead(file, buff, 0, size) < 0)
        {
            goto exit_err;
        }
    }
    return 0;

exit_err:
    handshakeSPI(STOP_HS);
    return -1;
}

static int handshakeWriteSPI(bert_host_state state,
                             char *file, char *buff,
                             uint32_t size)
{
    uint32_t j = 0;

    if (state == HOST_ON)
    {
        for (j = 0; j < size/BLOCK_SIZE; j++)
        {
            if (handshakeSPI(START_HS))
            {
                return -1;
            }
            if (spinorfsWrite(file, buff + j*BLOCK_SIZE,
                              j*BLOCK_SIZE, BLOCK_SIZE) < 0)
            {
                goto exit_err;
            }
            handshakeSPI(STOP_HS);
        }
        if (handshakeSPI(START_HS))
        {
            return -1;
        }
        if (spinorfsWrite(file, buff + j*BLOCK_SIZE,
                          j*BLOCK_SIZE,
                          size - j*BLOCK_SIZE) < 0)
        {
            goto exit_err;
        }
        handshakeSPI(STOP_HS);
    }
    else
    {
        if (spinorfsWrite(file, buff, 0, size) < 0)
        {
            goto exit_err;
        }
    }

    return 0;

exit_err:
    handshakeSPI(STOP_HS);
    return -1;
}

static int openSPINorDevice(int *fd)
{
    std::ifstream mtdInfoStream;
    std::string mtdDeviceStr;

    mtdInfoStream.open(PROC_MTD_INFO);
    std::string line;
    while (std::getline(mtdInfoStream, line))
    {
        info("Get line: {VALUE}", "VALUE", line.c_str());
        if (line.find(HOST_SPI_FLASH_MTD_NAME) != std::string::npos)
        {
            std::size_t pos = line.find(":");
            mtdDeviceStr = line.substr(0,pos);
            mtdDeviceStr = "/dev/" + mtdDeviceStr;
            *fd = open(mtdDeviceStr.c_str(), O_SYNC | O_RDWR);
            return 0;
        }
    }
    return -1;
}

static int initSPIDevice(bert_host_state state, int *fd)
{
    uint32_t size = 0, offset = 0;

    if ((state == HOST_ON) && handshakeSPI(START_HS))
    {
        goto exit_err;
    }
    if (openSPINorDevice(fd))
    {
        error("Can not open SPINOR device");
        goto exit_err;
    }
    if (spinorfs_gpt_disk_info(*fd, 0))
    {
        error("Get GPT Info failure");
        goto exit_err;
    }
    if (spinorfs_gpt_part_name_info((char*) bertNvp.c_str(), &offset, &size))
    {
        error("Get GPT Partition Info failure");
        goto exit_err;
    }
    if (spinorfs_mount(*fd, size, offset))
    {
        error("Mount Partition failure");
        goto exit_err;
    }
    if (state == HOST_ON)
        handshakeSPI(STOP_HS);

    return 0;
exit_err:
    if (*fd != -1)
    {
        close(*fd);
    }
    if (state == HOST_ON)
    {
        handshakeSPI(STOP_HS);
    }
    return -1;
}

static int initSPIDeviceRetry(bert_host_state state, int *fd, int num_retry)
{
    int i = num_retry;

    while (i > 0)
    {
        if (!initSPIDevice(state, fd))
            return 0;
        disableAccessHostSpiNor();
        sleep(0.5);
        enableAccessHostSpiNor();
        i--;
    }
    return -1;
}

static int handshakeSPIHandler(sdbusplus::bus::bus& bus, bert_host_state state)
{
    int ret = 0;
    uint8_t i;
    int devFd = -1;
    std::string bertDumpPath, bertFileNvpInfoPath, faultLogFilePath;
    std::string prefix, primaryLogId;
    AmpereBertPartitionInfo bertInfo;
    AmpereBertPayloadSection *bertPayload;
    bool isValidBert = false;

    ret = initSPIDeviceRetry(state, &devFd, NUM_RETRY);
    if (ret)
    {
        error("Init SPI Device failure");
        return ret;
    }
    /* Read Bert Partition Info from latest.ras */
    ret = handshakeReadSPI(state, (char*) bertFileNvp.c_str(),
                           (char*) &bertInfo, sizeof(AmpereBertPartitionInfo));
    if (ret)
    {
        error("Read {VALUE} failure", "VALUE", bertFileNvp.c_str());
        goto exit;
    }
#ifdef BERT_DEBUG
   for (int i = 0; i < BERT_MAX_NUM_FILE; i++)
   {
       std::cerr << "BERT_PARTITION_INFO size = " <<
                    bertInfo.files[i].size << "\n";
       std::cerr << "BERT_PARTITION_INFO name = " <<
                    bertInfo.files[i].name << "\n";
       std::cerr << "BERT_PARTITION_INFO flags = " <<
                    bertInfo.files[i].flags.reg << "\n";
   }
#endif
    for (i = 0; i < BERT_MAX_NUM_FILE; i++)
    {
        if(!bertInfo.files[i].flags.member.valid ||
           !bertInfo.files[i].flags.member.pendingBMC)
        {
            continue;
        }
        /*
         * Valid bert header and BMC flag is not set imply a new
         * bert record for BMC
         */
        bertDumpPath = std::string(BERT_LOG_DIR) +
                       std::string(bertInfo.files[i].name);
        std::vector<char> crashBufVector(bertInfo.files[i].size, 0);
        char *crashBuf = crashBufVector.data();
        ret = handshakeReadSPI(state, bertInfo.files[i].name,
                               crashBuf, bertInfo.files[i].size);
        if (!ret)
        {
            std::ofstream out (bertDumpPath.c_str(), std::ofstream::binary);
            if(!out.is_open())
            {
                error("Can not open ofstream for {VALUE}", "VALUE", bertDumpPath.c_str());
                continue;
            }
            out.write(crashBuf, bertInfo.files[i].size);
            out.close();
            /* Set BMC flag to 0 to indicated processed by BMC */
            bertInfo.files[i].flags.member.pendingBMC = 0;

#ifdef BERT_DEBUG
            bertPayload = (AmpereBertPayloadSection *) crashBuf;
            std::cerr << "firmwareVersion = " << bertPayload->firmwareVersion << "\n";
            std::cerr << "totalBertLength = " << bertPayload->totalBertLength << "\n";
            std::cerr << "sectionType = " << bertPayload->header.sectionType << "\n";
            std::cerr << "sectionLength = " << bertPayload->header.sectionLength << "\n";
            std::cerr << "sectionInstance = " << bertPayload->header.sectionInstance << "\n";
            std::cerr << "sectionsValid = " << bertPayload->sectionsValid.reg << "\n";
#endif
            std::string prefix = "RAS_BERT_";
            std::string type = "BERT";
            primaryLogId = crashcapture::utils::getUniqueEntryID(prefix);
            faultLogFilePath = std::string(CRASHDUMP_LOG_PATH) + primaryLogId;
            std::filesystem::copy(bertDumpPath.c_str(), faultLogFilePath.c_str(),
                    std::filesystem::copy_options::overwrite_existing);
            std::filesystem::remove(bertDumpPath.c_str());
            /* Add SEL and Redfish */
            bertPayload = (AmpereBertPayloadSection *) crashBuf;
            AmpereGenericHeader *cperData = &(bertPayload->genericHeader);
            addBertSELLog(bus, i, bertPayload->header.sectionType, cperData->subTypeId);
            crashcapture::utils::addFaultLogToRedfish(bus, primaryLogId, type);
            isValidBert = true;
        }
        else
        {
            error("Read {VALUE} failure", "VALUE", bertInfo.files[i].name);
            continue;
        }
    }
    if (!isValidBert)
    {
        goto exit;
    }

    /* Write back to BERT file info to indicate BMC consumed BERT record */
    ret = handshakeWriteSPI(state, (char*) bertFileNvp.c_str(),
                            (char*) &bertInfo,
                            sizeof(AmpereBertPartitionInfo));
    if (ret < 0) {
        error("Update {VALUE} failure", "VALUE", bertFileNvp.c_str());
        goto exit;
    }

exit:
    if (devFd != -1)
    {
        close(devFd);
    }
    spinorfs_unmount();
    return ret;
}

int bertHandler(sdbusplus::bus::bus& bus, bert_host_state state)
{
    int ret = 0;

    if (!std::filesystem::is_directory(BERT_LOG_DIR))
    {
         std::filesystem::create_directories(BERT_LOG_DIR);
    }
    if (!std::filesystem::is_directory(CRASHDUMP_LOG_PATH))
    {
        std::filesystem::create_directories(CRASHDUMP_LOG_PATH);
    }

    if (enableAccessHostSpiNor())
    {
        error("Cannot enable access SPI-NOR");
        return -1;
    }

    ret = handshakeSPIHandler(bus, state);

    if (disableAccessHostSpiNor())
    {
        error("Cannot disable access SPI-NOR");
        return -1;
    }

    return ret;
}

int maskPowerControl(bool mask)
{
    int ret = 0;
    std::string cmdReboot, cmdOff;

    if (mask && !isMasked)
    {
        cmdReboot = std::string(POWER_CONTROL_LOCK_SCRIPT) + " reboot false";
        cmdOff = std::string(POWER_CONTROL_LOCK_SCRIPT) + " off false";
        isMasked = true;
    }
    else if (!mask && isMasked)
    {
        cmdReboot = std::string(POWER_CONTROL_LOCK_SCRIPT) + " reboot true";
        cmdOff = std::string(POWER_CONTROL_LOCK_SCRIPT) + " off true";
        isMasked = false;
    }
    else
    {
        return ret;
    }
    ret = system(cmdReboot.c_str());
    ret += system(cmdOff.c_str());
    if (ret)
    {
        std::string value = mask ? "mask":"unmask";
        error("Cannot {VALUE} power control", "VALUE", value);
    }

    return ret;
}
