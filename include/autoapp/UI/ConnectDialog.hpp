#pragma once

#include <QDialog>
#include <QStringListModel>
#include "aasdk/TCP/ITCPEndpoint.hpp"
#include "aasdk/TCP/ITCPWrapper.hpp"
#include "openauto/Configuration/IRecentAddressesList.hpp"

namespace Ui {
class ConnectDialog;
}

namespace autoapp
{
namespace ui
{

class ConnectDialog : public QDialog
{
    Q_OBJECT

public:
    explicit ConnectDialog(boost::asio::io_service& ioService,  aasdk::tcp::ITCPWrapper& tcpWrapper, openauto::configuration::IRecentAddressesList& recentAddressesList, QWidget *parent = nullptr);
    ~ConnectDialog() override;

signals:
    void connectToDevice(const QString& ipAddress);
    void connectionSucceed(aasdk::tcp::ITCPEndpoint::SocketPointer socket, const std::string& ipAddress);
    void connectionFailed(const QString& message);

private slots:
    void onConnectButtonClicked();
    void onConnectionFailed(const QString& message);
    void onConnectionSucceed(aasdk::tcp::ITCPEndpoint::SocketPointer socket, const std::string& ipAddress);
    void onRecentAddressClicked(const QModelIndex& index);

private:
    void insertIpAddress(const std::string& ipAddress);
    void loadRecentList();
    void setControlsEnabledStatus(bool status);
    void connectHandler(const boost::system::error_code& ec, const std::string& ipAddress, aasdk::tcp::ITCPEndpoint::SocketPointer socket);

    boost::asio::io_service& ioService_;
    aasdk::tcp::ITCPWrapper& tcpWrapper_;
    openauto::configuration::IRecentAddressesList& recentAddressesList_;
    Ui::ConnectDialog *ui_;
    QStringListModel recentAddressesModel_;
};

}
}
