#include <QNetworkInterface>
#include <QThread>
#include "OpenautoLog.hpp"
#include "btservice/AndroidBluetoothServer.hpp"

namespace openauto
{
namespace btservice
{

AndroidBluetoothServer::AndroidBluetoothServer(openauto::configuration::IConfiguration::Pointer config)
    : rfcommServer_(std::make_unique<QBluetoothServer>(QBluetoothServiceInfo::RfcommProtocol, this))
    , socket_(nullptr)
    , config_(std::move(config))
{
    connect(rfcommServer_.get(), &QBluetoothServer::newConnection, this, &AndroidBluetoothServer::onClientConnected);
}

bool AndroidBluetoothServer::start(const QBluetoothAddress& address, uint16_t portNumber)
{
    LOG(info) << "listening.";
    return rfcommServer_->listen(address, portNumber);
}

void AndroidBluetoothServer::onClientConnected()
{
    socket_ = rfcommServer_->nextPendingConnection();
    if(socket_ != nullptr)
    {
        LOG(info) << "Device Connected: " << socket_->peerName().toStdString();
        connect(socket_, SIGNAL(readyRead()), this, SLOT(readSocket()));
        writeSocketInfoRequest();
    }
    else
    {
        LOG(error) << "received null socket during client connection.";
    }
}

bool AndroidBluetoothServer::writeProtoMessage(uint16_t messageType, google::protobuf::Message& message)
{
    QByteArray byteArray(message.SerializeAsString().c_str(), message.ByteSize());
    uint16_t messageLength = message.ByteSize();
    byteArray.prepend(messageType & 0x000000ff);
    byteArray.prepend((messageType & 0x0000ff00) >> 8);
    byteArray.prepend(messageLength & 0x000000ff);
    byteArray.prepend((messageLength & 0x0000ff00) >> 8);

    if(socket_->write(byteArray) != byteArray.length())
    {
        return false;
    }

    return true;
}

void AndroidBluetoothServer::writeSocketInfoRequest()
{
    LOG(info) << "Sending SocketInfoRequest.";

    QString ipAddr;
    foreach(QHostAddress addr, QNetworkInterface::allAddresses())
    {
        if(!addr.isLoopback() && (addr.protocol() == QAbstractSocket::IPv4Protocol))
        {
            ipAddr = addr.toString();
        }
    }

    btservice::proto::SocketInfoRequest socketInfoRequest;
    socketInfoRequest.set_ip_address(ipAddr.toStdString());
    LOG(info) << "ipAddress: "<< ipAddr.toStdString();

    socketInfoRequest.set_port(5000);
    LOG(info) << "port: "<< 5000;

    if(this->writeProtoMessage(1, socketInfoRequest))
    {
        LOG(info) << "Sent SocketInfoRequest.";
    }
    else
    {
        LOG(error) << "Error sending SocketInfoRequest.";
    }
}
void AndroidBluetoothServer::writeSocketInfoResponse()
{
    LOG(info) << "Sending SocketInfoResponse.";
    QString ipAddr;
    foreach(QHostAddress addr, QNetworkInterface::allAddresses())
    {
        if(!addr.isLoopback() && (addr.protocol() == QAbstractSocket::IPv4Protocol))
        {
            ipAddr = addr.toString();
        }
    }

    btservice::proto::SocketInfoResponse socketInfoResponse;
    socketInfoResponse.set_ip_address(ipAddr.toStdString());
    LOG(info) << "ipAddress: "<< ipAddr.toStdString();

    socketInfoResponse.set_port(5000);
    LOG(info) << "port: "<< 5000;

    socketInfoResponse.set_status(btservice::proto::Status::STATUS_SUCCESS);
    LOG(info) << "status: "<< btservice::proto::Status::STATUS_SUCCESS;


    if(this->writeProtoMessage(7, socketInfoResponse))
    {
        LOG(info) << "Sent SocketInfoResponse.";
    }
    else
    {
        LOG(error) << "Error sending SocketInfoResponse.";
    }
}

void AndroidBluetoothServer::handleSocketInfoRequestResponse(QByteArray data)
{
    btservice::proto::SocketInfoResponse socketInfoResponse;
    socketInfoResponse.ParseFromArray(data, data.size());
    LOG(info) <<"Received SocketInfoRequestResponse, status: "<<socketInfoResponse.status();
    if(socketInfoResponse.status() == 0)
    {
        // A status of 0 should be successful handshake (unless phone later reports an error, aw well)
        // save this phone so we can autoconnect to it next time
        config_->setLastBluetoothPair(socket_->peerAddress().toString().toStdString());
        config_->save();
    }
}


void AndroidBluetoothServer::handleSocketInfoRequest(QByteArray data)
{
    LOG(info) << "Reading SocketInfoRequest.";

    btservice::proto::SocketInfoRequest socketInfoRequest;
    
    writeSocketInfoResponse();
}

void AndroidBluetoothServer::writeNetworkInfoMessage()
{
    LOG(info) << "Sending NetworkInfoMessage.";

    btservice::proto::NetworkInfo networkMessage;
    networkMessage.set_ssid(config_->getWifiSSID());
    LOG(info) << "SSID: "<<config_->getWifiSSID();

    networkMessage.set_psk(config_->getWifiPassword());
    LOG(info) << "PSKEY: "<<config_->getWifiPassword();

    if(config_->getWifiMAC().empty())
    {
        networkMessage.set_mac_addr(QNetworkInterface::interfaceFromName("wlan0").hardwareAddress().toStdString());
        LOG(info) << "MAC: "<<QNetworkInterface::interfaceFromName("wlan0").hardwareAddress().toStdString();
    }
    else
    {
        networkMessage.set_mac_addr(config_->getWifiMAC());
        LOG(info) << "MAC: "<< config_->getWifiMAC();
    }

    networkMessage.set_security_mode(btservice::proto::SecurityMode::WPA2_PERSONAL);
    LOG(info) << "Security: "<< btservice::proto::SecurityMode::WPA2_PERSONAL;

    networkMessage.set_ap_type(btservice::proto::AccessPointType::STATIC);
    LOG(info) << "AP Type: "<< btservice::proto::AccessPointType::STATIC;


    if(this->writeProtoMessage(3, networkMessage))
    {
        LOG(info) << "Sent NetworkInfoMessage";
    }
    else
    {
        LOG(info) << "Error sending NetworkInfoMessage.";
    }
}

void AndroidBluetoothServer::handleUnknownMessage(int messageType, QByteArray data)
{
        LOG(info) << "Received unknown MessageType of "<<messageType;
        LOG(info) << "Unknown Message Data: "<<data.toHex(' ').toStdString()  ;
}

void AndroidBluetoothServer::readSocket()
{
    if(!socket_)
    {
        return;
    }

    auto data = socket_->readAll();
    if(data.length() == 0)
    {
        return;
    }

    uint16_t messageType = (data[2] << 8) | data[3];
    switch(messageType)
    {
        case 1:
            handleSocketInfoRequest(data);
            break;
        case 2:
            writeNetworkInfoMessage();
            break;
        case 7:
            data.remove(0, 4);
            handleSocketInfoRequestResponse(data);
            break;
        default:
            data.remove(0, 4);
            handleUnknownMessage(messageType, data);
            break;
    }
}

}
}
