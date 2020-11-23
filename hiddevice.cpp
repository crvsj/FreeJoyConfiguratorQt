#include <QThread>
#include <QDebug>
#include <QElapsedTimer>
#include "hiddevice.h"
#include "hidapi.h"
#include "common_defines.h"
#include "firmwareupdater.h"

#define VID 0x0483  //0x0483

void HidDevice::processData()
{
    QElapsedTimer timer;
    timer.start();

    int res = 0;
    bool change = false;
    bool noDeviceSent = false;
    //bool name_checked = false;
    m_flasher = nullptr;
    QList<hid_device_info*> tmp_HidDevicesAdrList;
    hid_device_info* hidDevInfo;
    QStringList strList;
    uint8_t buffer[BUFFSIZE]={0};

    m_currentWork = REPORT_ID_JOY;

    while (m_isFinish == false)
    {
        // check connected devices
        if (!change)
        {
            timer.start();
            change = true;
        }
        else if (change && timer.elapsed() > 800)   // change is always true
        {
            hidDevInfo = hid_enumerate(VID, 0x0);
            if (!hidDevInfo && noDeviceSent == false)
            {
                strList.clear();
                m_HidDevicesAdrList.clear();
                emit hidDeviceList(&strList);
                noDeviceSent = true;
                //name_checked = false;
            }

            while(hidDevInfo)
            {
                if(QString::fromWCharArray(hidDevInfo->product_string) == "FreeJoy Flasher"){
                    if (!m_flasher){
                        qDebug()<<"processData - Flasher found";
                        m_flasher = hidDevInfo;    // первый
                        emit flasherFound(true);
                    }
                    m_flasher = hidDevInfo;    // второй раз?
                    hidDevInfo = hidDevInfo->next;
                    if (m_currentWork == REPORT_ID_FIRMWARE)    // дерьма накодил?
                    {
                        flashFirmwareToDevice();
                        m_currentWork = REPORT_ID_JOY;
                    }
                    continue;
                }

                tmp_HidDevicesAdrList.append(hidDevInfo);
                hidDevInfo = hidDevInfo->next;
                if (!hidDevInfo && m_HidDevicesAdrList.size() != tmp_HidDevicesAdrList.size())
                {
                    //QThread::msleep(20);   // хз
                    m_HidDevicesAdrList.clear();
                    strList.clear();
                    noDeviceSent = false;
                    for (int i = 0; i < tmp_HidDevicesAdrList.size(); ++i)
                    {
                        m_HidDevicesAdrList.append(tmp_HidDevicesAdrList[i]);
                        strList << QString::fromWCharArray(tmp_HidDevicesAdrList[i]->product_string);
                    }
                    emit hidDeviceList(&strList);
                    tmp_HidDevicesAdrList.clear();
                }
            }
            tmp_HidDevicesAdrList.clear();
            change = false;
        }

        // no device
        if (!m_handleRead)
        {
            if (m_HidDevicesAdrList.size()){          // ?
                m_handleRead = hid_open(VID, m_HidDevicesAdrList[0]->product_id,nullptr);
            }
            if (!m_handleRead) {
                //name_checked = false;
                emit putDisconnectedDeviceInfo();
                //hid_free_enumeration(hid_dev_info);
                QThread::msleep(500);
            } else {
                emit putConnectedDeviceInfo();
            }
        }
        // device connected
        if (m_handleRead)
        {

//            if (name_checked == false)
//            {
//                for (int i = 0; i < str_list.size(); ++i){
//                    if (str_list[i] == ""){
//                        qDebug()<<"No product string, repeat";
//                        str_list[i] =QString::fromWCharArray(HidDevicesAdrList[i]->product_string);
//                        emit hidDeviceList(&str_list);
//                        //qDebug()<<QString::fromWCharArray(str);
//                    }
//                }
//                name_checked = true;
//            }

            // read joy report
            if (m_currentWork == REPORT_ID_JOY)
            {
                res=hid_read_timeout(m_handleRead, buffer, BUFFSIZE,10000);         // 10000?
                if (res < 0) {
                    hid_close(m_handleRead);
                    m_handleRead=nullptr;
                } else {
                    if (buffer[0] == REPORT_ID_JOY) {   // перестраховка
                        memset(m_deviceBuffer, 0, BUFFSIZE);
                        memcpy(m_deviceBuffer, buffer, BUFFSIZE);
                        emit putGamepadPacket(m_deviceBuffer);
                    }
                }
            }
            // read config from device
            else if (m_currentWork == REPORT_ID_CONFIG_IN)
            {
                readConfigFromDevice(buffer);
            }
            // write config to device
            else if (m_currentWork == REPORT_ID_CONFIG_OUT)
            {
                writeConfigToDevice(buffer);
//                HidDevicesAdrList.clear();      // очистка спика устройств после записи конфига
//                str_list.clear();               // чтобы небыло бага в имени при выборе устройства
//                no_device_sent = false;         // говнокод
//                hid_free_enumeration(hid_dev_info);
            }
//            else if (current_work_ == REPORT_ID_FIRMWARE)
//            {
//                EnterToFlashMode();
//            }
        }
    }
    hid_free_enumeration(hidDevInfo);            // ????
}

// stop processData, close app
void HidDevice::setIsFinish(bool is_finish)
{
    m_isFinish = is_finish;
}

// read config
void HidDevice::readConfigFromDevice(uint8_t *buffer)
{
    QElapsedTimer timer;
    timer.start();
    int res = 0;
    qint64 start_time = 0;
    qint64 resend_time = 0;
    int report_count = 0;
    uint8_t config_request_buffer[2] = {REPORT_ID_CONFIG_IN, 1};

    start_time = timer.elapsed();
    resend_time = start_time;
    hid_write(m_handleRead, config_request_buffer, 2);

    while (timer.elapsed() < start_time + 2000)
    {
        if (m_handleRead)    // перестаховка
        {
            res=hid_read_timeout(m_handleRead, buffer, BUFFSIZE,100);
            if (res < 0) {
                hid_close(m_handleRead);
                m_handleRead=nullptr;
            }
            else
            {
                if (buffer[0] == REPORT_ID_CONFIG_IN)
                {
                    if (buffer[1] == config_request_buffer[1])
                    {
                        gEnv.pDeviceConfig->config = m_reportConvert->getConfigFromDevice(buffer);
                        config_request_buffer[1] += 1;
                        hid_write(m_handleRead, config_request_buffer, 2);
                        report_count++;
                        qDebug()<<"Config"<<report_count<<"received";
                        if (config_request_buffer[1] > CONFIG_COUNT)
                        {
                            break;
                        }
                    }
                }
                else if (config_request_buffer[1] < 2 && (resend_time + 250 - timer.elapsed()) <= 0) // for first packet
                {
                    qDebug() << "Resend activated";
                    config_request_buffer[1] = 1;
                    resend_time = timer.elapsed();
                    hid_write(m_handleRead, config_request_buffer, 2);
                }
            }
        } else {    // перестаховка
            m_currentWork = REPORT_ID_JOY;
            emit configReceived(false);
            break;
        }
    }
    qDebug()<<"read report_count ="<<report_count<<"/"<<CONFIG_COUNT;
    if (report_count == CONFIG_COUNT){
        qDebug() << "All config received";
    } else {
        qDebug() << "ERROR, not all config received";
    }

    if (report_count == CONFIG_COUNT) {
        m_currentWork = REPORT_ID_JOY;
        emit configReceived(true);
    } else {
        m_currentWork = REPORT_ID_JOY;
        emit configReceived(false);
    }
}

// write config
void HidDevice::writeConfigToDevice(uint8_t *buffer)
{
    QElapsedTimer timer;
    timer.start();
    int res = 0;
    qint64 start_time = 0;
    qint64 resend_time = 0;
    int report_count = 0;
    uint8_t config_out_buffer[BUFFSIZE] = {REPORT_ID_CONFIG_OUT, 0};

    start_time = timer.elapsed();
    resend_time = start_time;
    hid_write(m_handleRead, config_out_buffer, BUFFSIZE);

    while (timer.elapsed() < start_time + 2000)
    {
        if (m_handleRead)    // перестаховка
        {
            res=hid_read_timeout(m_handleRead, buffer, BUFFSIZE,100);
            if (res < 0) {
                hid_close(m_handleRead);
                m_handleRead=nullptr;
            }
            else
            {
                if (buffer[0] == REPORT_ID_CONFIG_OUT)
                {
                    if (buffer[1] == config_out_buffer[1] + 1)
                    {
                        config_out_buffer[1] += 1;
                        std::vector<uint8_t> tmp_buf = m_reportConvert->sendConfigToDevice(config_out_buffer[1]);
                        //memcpy((uint8_t*)(config_buffer), tmp, BUFFSIZE);
                        for (int i = 2; i < 64; i++)
                        {                                       // какой пиздец
                            config_out_buffer[i] = tmp_buf[i];
                        }

                        hid_write(m_handleRead, config_out_buffer, BUFFSIZE);
                        report_count++;
                        qDebug()<<"Config"<<report_count<<"sent";

                        if (buffer[1] == CONFIG_COUNT){
                            break;
                        }
                    }
                }
                else if (config_out_buffer[1] == 0 && (resend_time + 250 - timer.elapsed()) <= 0) // for first packet
                {
                    qDebug() << "Resend activated";
                    resend_time = timer.elapsed();
                    hid_write(m_handleRead, config_out_buffer, BUFFSIZE);
                }
            }
        } else {    // перестаховка
            m_currentWork = REPORT_ID_JOY;
            emit configSent(false);
            break;
        }
    }
    qDebug()<<"write report_count ="<<report_count<<"/"<<CONFIG_COUNT;
    if (report_count == CONFIG_COUNT){
        qDebug() << "All config sent";
    } else {
        qDebug() << "ERROR, not all config sent";
    }

    if (report_count == CONFIG_COUNT) {
        m_currentWork = REPORT_ID_JOY;
        emit configSent(true);
    } else {
        m_currentWork = REPORT_ID_JOY;
        emit configSent(false);
    }
}

// flash firmware
void HidDevice::flashFirmwareToDevice()
{
    qDebug()<<"flash size = "<<m_firmware->size();
    if(m_flasher)
    {
        hid_device* flasher = hid_open(VID, m_flasher->product_id, m_flasher->serial_number);;
        qint64 millis;
        QElapsedTimer time;
        time.start();
        millis = time.elapsed();
        uint8_t flash_buffer[BUFFSIZE]{};
        uint8_t flasher_device_buffer[BUFFSIZE]{};
        uint16_t length = (uint16_t)m_firmware->size();
        uint16_t crc16 = FirmwareUpdater::computeChecksum(m_firmware);
        int update_percent = 0;

        flash_buffer[0] = REPORT_ID_FIRMWARE;
        flash_buffer[1] = 0;
        flash_buffer[2] = 0;
        flash_buffer[3] = 0;
        flash_buffer[4] = (uint8_t)(length & 0xFF);
        flash_buffer[5] = (uint8_t)(length >> 8);
        flash_buffer[6] = (uint8_t)(crc16 & 0xFF);
        flash_buffer[7] = (uint8_t)(crc16 >> 8);

        hid_write(flasher, flash_buffer, BUFFSIZE);

        int res = 0;
        uint8_t buffer[BUFFSIZE]={0};
        while (time.elapsed() < millis + 30000) // 30 сек на прошивку
        {
            if (flasher){
                res=hid_read_timeout(flasher, buffer, BUFFSIZE,5000);  // ?
                if (res < 0) {
                    hid_close(flasher);
                    flasher=nullptr;
                    //flasher_ = nullptr;
                    break;
                } else {
                    if (buffer[0] == REPORT_ID_FIRMWARE) {
                        memset(flasher_device_buffer, 0, BUFFSIZE);
                        memcpy(flasher_device_buffer, buffer, BUFFSIZE);
                    }
                }
            }

            if (flasher_device_buffer[0] == REPORT_ID_FIRMWARE)
            {
                uint16_t cnt = (uint16_t)(flasher_device_buffer[1] << 8 | flasher_device_buffer[2]);
                if ((cnt & 0xF000) == 0xF000)  // status packet
                {
                    //qDebug()<<"ERROR";
                    if (cnt == 0xF001)  // firmware size error
                    {
                        hid_close(flasher);
                        //flasher_ = nullptr;
                        emit flashStatus(SIZE_ERROR, update_percent);
                        break;
                    }
                    else if (cnt == 0xF002) // CRC error
                    {
                        hid_close(flasher);
                        //flasher_ = nullptr;
                        emit flashStatus(CRC_ERROR, update_percent);
                        break;
                    }
                    else if (cnt == 0xF003) // flash erase error
                    {
                        hid_close(flasher);
                        //flasher_ = nullptr;
                        emit flashStatus(ERASE_ERROR, update_percent);
                        break;
                    }
                    else if (cnt == 0xF000) // OK
                    {
                        hid_close(flasher);
                        //flasher_ = nullptr;
                        emit flashStatus(FINISHED, update_percent);
                        break;
                    }
                }
                else
                {
                    qDebug()<<"Firmware packet requested:"<<cnt;

                    flash_buffer[0] = REPORT_ID_FIRMWARE;
                    flash_buffer[1] = (uint8_t)(cnt >> 8);
                    flash_buffer[2] = (uint8_t)(cnt & 0xFF);
                    flash_buffer[3] = 0;

                    if (cnt * 60 < m_firmware->size())
                    {
                        memcpy(flash_buffer +4, m_firmware->constData() + (cnt - 1) * 60, 60);
                        update_percent = ((cnt - 1) * 60 * 100 / m_firmware->size());
                        hid_write(flasher, flash_buffer, 64);
                        emit flashStatus(IN_PROCESS, update_percent);

                        qDebug()<<"Firmware packet sent:"<<cnt;
                    }
                    else
                    {
                        memcpy(flash_buffer +4, m_firmware->constData() + (cnt - 1) * 60, m_firmware->size() - (cnt - 1) * 60);     // file_bytes->size() для 32 и 64 бит одинаков?
                        update_percent = 0;
                        hid_write(flasher, flash_buffer, 64);
                        emit flashStatus(IN_PROCESS, update_percent);

                        qDebug()<<"Firmware packet sent:"<<cnt;
                    }
                }
            }
        }
    }
}

// button "get config" clicked
void HidDevice::getConfigFromDevice()
{
    m_currentWork = REPORT_ID_CONFIG_IN;
}
// button "send config" clicked
void HidDevice::sendConfigToDevice()
{
    m_currentWork = REPORT_ID_CONFIG_OUT;
}
// button "flash firmware" clicked
void HidDevice::flashFirmware(const QByteArray* firmware)
{
    m_firmware = firmware;
    m_currentWork = REPORT_ID_FIRMWARE;
}

bool HidDevice::enterToFlashMode()
{
    if(m_handleRead)
    {
        m_flasher = nullptr;
        qint64 millis;
        QElapsedTimer time;
        time.start();
        millis = time.elapsed();
        qDebug()<<"before hid_write";
        uint8_t config_buffer[64] = {REPORT_ID_FIRMWARE,'b','o','o','t','l','o','a','d','e','r',' ','r','u','n'};
        hid_write(m_handleRead, config_buffer, 64);
        qDebug()<<"after hid_write";
        while (time.elapsed() < millis + 1000)
        {
            if (m_flasher){
                return true;
            }
        }
    }
    return false;
}


// another device selected in comboBox
void HidDevice::setSelectedDevice(int device_number)        // заблочить сигнал до запуска, скорее всего крашит из-за разных потоков
{                                                           // только в винде. решил костылём в hidapi.c
    if (device_number < 0){
        //device_number = 0;
        return;
    } else if (device_number > m_HidDevicesAdrList.size() - 1){
        device_number = m_HidDevicesAdrList.size() - 1;
    }
    m_selectedDevice = device_number; 
    qDebug()<<"HID open start";
    qDebug()<<device_number + 1<<"devices connected";
        // возможно не стоит здесь открывать, оставить изменение selected_device_, а открытие в processData()
    m_handleRead = hid_open(VID, m_HidDevicesAdrList[m_selectedDevice]->product_id, m_HidDevicesAdrList[m_selectedDevice]->serial_number);

//    if (m_handleRead) {
//        emit putConnectedDeviceInfo();
//    } else {
//        emit putDisconnectedDeviceInfo();
//    }

#ifdef _WIN32
    qDebug()<<"Unsuccessful serial number attempts ="<<GetSerialNumberAttemption()<<"(not a error)";
    qDebug()<<"Unsuccessful product string attempts ="<<GetProductStrAttemption()<<"(not a error)";
#endif
    qDebug()<<"HID opened";
}
