/*
*  This file is part of openauto project.
*  Copyright (C) 2018 f1x.studio (Michal Szwaj)
*
*  openauto is free software: you can redistribute it and/or modify
*  it under the terms of the GNU General Public License as published by
*  the Free Software Foundation; either version 3 of the License, or
*  (at your option) any later version.

*  openauto is distributed in the hope that it will be useful,
*  but WITHOUT ANY WARRANTY; without even the implied warranty of
*  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
*  GNU General Public License for more details.
*
*  You should have received a copy of the GNU General Public License
*  along with openauto. If not, see <http://www.gnu.org/licenses/>.
*/

#include <thread>
#include "aasdk/USB/AOAPDevice.hpp"
#include "aasdk/TCP/TCPEndpoint.hpp"
#include "openauto/App.hpp"
#include "OpenautoLog.hpp"

namespace openauto
{

App::App(boost::asio::io_service& ioService, aasdk::usb::USBWrapper& usbWrapper, aasdk::tcp::ITCPWrapper& tcpWrapper, openauto::service::IAndroidAutoEntityFactory& androidAutoEntityFactory,
         aasdk::usb::IUSBHub::Pointer usbHub, aasdk::usb::IConnectedAccessoriesEnumerator::Pointer connectedAccessoriesEnumerator)
    : ioService_(ioService)
    , usbWrapper_(usbWrapper)
    , tcpWrapper_(tcpWrapper)
    , strand_(ioService_)
    , androidAutoEntityFactory_(androidAutoEntityFactory)
    , usbHub_(std::move(usbHub))
    , acceptor_(ioService_, boost::asio::ip::tcp::endpoint(boost::asio::ip::tcp::v4(), 5000))
    , connectedAccessoriesEnumerator_(std::move(connectedAccessoriesEnumerator))
    , isStopped_(false)
{

}

void App::waitForDevice(bool enumerate)
{
    this->waitForUSBDevice();
    this->waitForWirelessDevice();

    if(enumerate)
    {
        strand_.dispatch([this, self = this->shared_from_this()]() {
            this->enumerateDevices();
        });
    }
}

void App::start(aasdk::tcp::ITCPEndpoint::SocketPointer socket)
{
    LOG(info) << "Wireless Device connected.";

    strand_.dispatch([this, self = this->shared_from_this(), socket = std::move(socket)]() mutable {
        if(androidAutoEntity_ != nullptr)
        {
            tcpWrapper_.close(*socket);
            LOG(warning) << "android auto entity is still running.";
            return;
        }

        try
        {
            usbHub_->cancel();
            connectedAccessoriesEnumerator_->cancel();

            auto tcpEndpoint(std::make_shared<aasdk::tcp::TCPEndpoint>(tcpWrapper_, std::move(socket)));
            androidAutoEntity_ = androidAutoEntityFactory_.create(std::move(tcpEndpoint));
            androidAutoEntity_->start(*this);
        }
        catch(const aasdk::error::Error& error)
        {
            LOG(error) << "TCP AndroidAutoEntity create error: " << error.what();

            androidAutoEntity_.reset();
            this->waitForDevice();
        }
    });
}

void App::stop()
{
    strand_.dispatch([this, self = this->shared_from_this()]() {
        isStopped_ = true;
        connectedAccessoriesEnumerator_->cancel();
        usbHub_->cancel();

        if(androidAutoEntity_ != nullptr)
        {
            androidAutoEntity_->stop();
            androidAutoEntity_.reset();
        }
    });
}

void App::aoapDeviceHandler(aasdk::usb::DeviceHandle deviceHandle)
{
    LOG(info) << "USB Device connected.";

    if(androidAutoEntity_ != nullptr)
    {
        LOG(warning) << "android auto entity is still running.";
        return;
    }

    try
    {
        connectedAccessoriesEnumerator_->cancel();

        auto aoapDevice(aasdk::usb::AOAPDevice::create(usbWrapper_, ioService_, deviceHandle));
        androidAutoEntity_ = androidAutoEntityFactory_.create(std::move(aoapDevice));
        androidAutoEntity_->start(*this);
    }
    catch(const aasdk::error::Error& error)
    {
        LOG(error) << "USB AndroidAutoEntity create error: " << error.what();

        androidAutoEntity_.reset();
        this->waitForDevice();
    }
}

void App::enumerateDevices()
{
    auto promise = aasdk::usb::IConnectedAccessoriesEnumerator::Promise::defer(strand_);
    promise->then([this, self = this->shared_from_this()](auto result) {
            LOG(info) << "Devices enumeration result: " << result;
        },
        [this, self = this->shared_from_this()](auto e) {
            LOG(error) << "Devices enumeration failed: " << e.what();
        });

    connectedAccessoriesEnumerator_->enumerate(std::move(promise));
}

void App::waitForUSBDevice()
{
    LOG(info) << "Waiting for USB device...";

    auto promise = aasdk::usb::IUSBHub::Promise::defer(strand_);
    promise->then(std::bind(&App::aoapDeviceHandler, this->shared_from_this(), std::placeholders::_1),
                  std::bind(&App::onUSBHubError, this->shared_from_this(), std::placeholders::_1));
    usbHub_->start(std::move(promise));
}

void App::waitForWirelessDevice()
{
    LOG(info) << "Waiting for Wireless device...";

    auto socket = std::make_shared<boost::asio::ip::tcp::socket>(ioService_);
    acceptor_.async_accept(*socket, [this, socket](const boost::system::error_code &) { this->start(socket); });
}

void App::onAndroidAutoQuit()
{
    strand_.dispatch([this, self = this->shared_from_this()]() {
        LOG(info) << "quit.";

        androidAutoEntity_->stop();
        androidAutoEntity_.reset();

        if(!isStopped_)
        {
            this->waitForDevice();
        }
    });
}

void App::onUSBHubError(const aasdk::error::Error& error)
{
    LOG(error) << "usb hub error: " << error.what();

    if(error != aasdk::error::ErrorCode::OPERATION_ABORTED &&
       error != aasdk::error::ErrorCode::OPERATION_IN_PROGRESS)
    {
        this->waitForDevice();
    }
}

}
