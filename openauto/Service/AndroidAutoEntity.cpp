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

#include "aasdk/Channel/Control/ControlServiceChannel.hpp"
#include "openauto/Service/AndroidAutoEntity.hpp"
#include "OpenautoLog.hpp"

namespace openauto
{
namespace service
{

AndroidAutoEntity::AndroidAutoEntity(boost::asio::io_service& ioService,
                                     aasdk::messenger::ICryptor::Pointer cryptor,
                                     aasdk::transport::ITransport::Pointer transport,
                                     aasdk::messenger::IMessenger::Pointer messenger,
                                     configuration::IConfiguration::Pointer configuration,
                                     ServiceList serviceList,
                                     IPinger::Pointer pinger)
    : strand_(ioService)
    , cryptor_(std::move(cryptor))
    , transport_(std::move(transport))
    , messenger_(std::move(messenger))
    , controlServiceChannel_(std::make_shared<aasdk::channel::control::ControlServiceChannel>(strand_, messenger_))
    , configuration_(std::move(configuration))
    , serviceList_(std::move(serviceList))
    , pinger_(std::move(pinger))
    , eventHandler_(nullptr)
{
}

AndroidAutoEntity::~AndroidAutoEntity()
{
    LOG(debug) << "destroy.";
}

void AndroidAutoEntity::start(IAndroidAutoEntityEventHandler& eventHandler)
{
    strand_.dispatch([this, self = this->shared_from_this(), eventHandler = &eventHandler]() {
        LOG(info) << "start.";

        eventHandler_ = eventHandler;
        std::for_each(serviceList_.begin(), serviceList_.end(), std::bind(&IService::start, std::placeholders::_1));
        this->schedulePing();

        auto versionRequestPromise = aasdk::channel::SendPromise::defer(strand_);
        versionRequestPromise->then([]() {}, std::bind(&AndroidAutoEntity::onChannelError, this->shared_from_this(), std::placeholders::_1));
        controlServiceChannel_->sendVersionRequest(std::move(versionRequestPromise));
        controlServiceChannel_->receive(this->shared_from_this());
    });
}

void AndroidAutoEntity::stop()
{
    strand_.dispatch([this, self = this->shared_from_this()]() {
        LOG(info) << "stop.";

        eventHandler_ = nullptr;
        std::for_each(serviceList_.begin(), serviceList_.end(), std::bind(&IService::stop, std::placeholders::_1));
        pinger_->cancel();
        messenger_->stop();
        transport_->stop();
        cryptor_->deinit();
    });
}

void AndroidAutoEntity::onVersionResponse(uint16_t majorCode, uint16_t minorCode, aasdk::proto::enums::VersionResponseStatus::Enum status)
{
    LOG(info) << "version response, version: " << majorCode
                       << "." << minorCode
                       << ", status: " << status;

    if(status == aasdk::proto::enums::VersionResponseStatus::MISMATCH)
    {
        LOG(error) << "version mismatch.";
        this->triggerQuit();
    }
    else
    {
        LOG(info) << "Begin handshake.";

        try
        {
            cryptor_->doHandshake();

            auto handshakePromise = aasdk::channel::SendPromise::defer(strand_);
            handshakePromise->then([]() {}, std::bind(&AndroidAutoEntity::onChannelError, this->shared_from_this(), std::placeholders::_1));
            controlServiceChannel_->sendHandshake(cryptor_->readHandshakeBuffer(), std::move(handshakePromise));
            controlServiceChannel_->receive(this->shared_from_this());
        }
        catch(const aasdk::error::Error& e)
        {
            this->onChannelError(e);
        }
    }
}

void AndroidAutoEntity::onHandshake(const aasdk::common::DataConstBuffer& payload)
{
    LOG(info) << "Handshake, size: " << payload.size;

    try
    {
        cryptor_->writeHandshakeBuffer(payload);

        if(!cryptor_->doHandshake())
        {
            LOG(info) << "continue handshake.";

            auto handshakePromise = aasdk::channel::SendPromise::defer(strand_);
            handshakePromise->then([]() {}, std::bind(&AndroidAutoEntity::onChannelError, this->shared_from_this(), std::placeholders::_1));
            controlServiceChannel_->sendHandshake(cryptor_->readHandshakeBuffer(), std::move(handshakePromise));
        }
        else
        {
            LOG(info) << "Auth completed.";

            aasdk::proto::messages::AuthCompleteIndication authCompleteIndication;
            authCompleteIndication.set_status(aasdk::proto::enums::Status::OK);

            auto authCompletePromise = aasdk::channel::SendPromise::defer(strand_);
            authCompletePromise->then([]() {}, std::bind(&AndroidAutoEntity::onChannelError, this->shared_from_this(), std::placeholders::_1));
            controlServiceChannel_->sendAuthComplete(authCompleteIndication, std::move(authCompletePromise));
        }

        controlServiceChannel_->receive(this->shared_from_this());
    }
    catch(const aasdk::error::Error& e)
    {
        this->onChannelError(e);
    }
}

void AndroidAutoEntity::onServiceDiscoveryRequest(const aasdk::proto::messages::ServiceDiscoveryRequest& request)
{
    LOG(info) << "Discovery request, device name: " << request.device_name()
                       << ", brand: " << request.device_brand();

    aasdk::proto::messages::ServiceDiscoveryResponse serviceDiscoveryResponse;
    serviceDiscoveryResponse.mutable_channels()->Reserve(256);
    serviceDiscoveryResponse.set_head_unit_name("OpenAuto");
    serviceDiscoveryResponse.set_car_model("Universal");
    serviceDiscoveryResponse.set_car_year("2018");
    serviceDiscoveryResponse.set_car_serial("20180301");
    serviceDiscoveryResponse.set_left_hand_drive_vehicle(configuration_->getHandednessOfTrafficType() == configuration::HandednessOfTrafficType::LEFT_HAND_DRIVE);
    serviceDiscoveryResponse.set_headunit_manufacturer("f1x");
    serviceDiscoveryResponse.set_headunit_model("OpenAuto Autoapp");
    serviceDiscoveryResponse.set_sw_build("1");
    serviceDiscoveryResponse.set_sw_version("1.0");
    serviceDiscoveryResponse.set_can_play_native_media_during_vr(false);
    serviceDiscoveryResponse.set_hide_clock(!configuration_->showClock());

    std::for_each(serviceList_.begin(), serviceList_.end(), std::bind(&IService::fillFeatures, std::placeholders::_1, std::ref(serviceDiscoveryResponse)));

    auto promise = aasdk::channel::SendPromise::defer(strand_);
    promise->then([]() {}, std::bind(&AndroidAutoEntity::onChannelError, this->shared_from_this(), std::placeholders::_1));
    controlServiceChannel_->sendServiceDiscoveryResponse(serviceDiscoveryResponse, std::move(promise));
    controlServiceChannel_->receive(this->shared_from_this());
}

void AndroidAutoEntity::onAudioFocusRequest(const aasdk::proto::messages::AudioFocusRequest& request)
{
    LOG(info) << "requested audio focus, type: " << request.audio_focus_type();

    aasdk::proto::enums::AudioFocusState::Enum audioFocusState =
            request.audio_focus_type() == aasdk::proto::enums::AudioFocusType::RELEASE ? aasdk::proto::enums::AudioFocusState::LOSS
                                                                                       : aasdk::proto::enums::AudioFocusState::GAIN;

    LOG(info) << "audio focus state: " << audioFocusState;

    aasdk::proto::messages::AudioFocusResponse response;
    response.set_audio_focus_state(audioFocusState);

    auto promise = aasdk::channel::SendPromise::defer(strand_);
    promise->then([]() {}, std::bind(&AndroidAutoEntity::onChannelError, this->shared_from_this(), std::placeholders::_1));
    controlServiceChannel_->sendAudioFocusResponse(response, std::move(promise));
    controlServiceChannel_->receive(this->shared_from_this());
}

void AndroidAutoEntity::onShutdownRequest(const aasdk::proto::messages::ShutdownRequest& request)
{
    LOG(info) << "Shutdown request, reason: " << request.reason();

    aasdk::proto::messages::ShutdownResponse response;
    auto promise = aasdk::channel::SendPromise::defer(strand_);
    promise->then(std::bind(&AndroidAutoEntity::triggerQuit, this->shared_from_this()),
                  std::bind(&AndroidAutoEntity::onChannelError, this->shared_from_this(), std::placeholders::_1));

    controlServiceChannel_->sendShutdownResponse(response, std::move(promise));
}

void AndroidAutoEntity::onShutdownResponse(const aasdk::proto::messages::ShutdownResponse&)
{
    LOG(info) << "Shutdown response ";
    this->triggerQuit();
}

void AndroidAutoEntity::onNavigationFocusRequest(const aasdk::proto::messages::NavigationFocusRequest& request)
{
    LOG(info) << "navigation focus request, type: " << request.type();

    aasdk::proto::messages::NavigationFocusResponse response;
    response.set_type(2);

    auto promise = aasdk::channel::SendPromise::defer(strand_);
    promise->then([]() {}, std::bind(&AndroidAutoEntity::onChannelError, this->shared_from_this(), std::placeholders::_1));
    controlServiceChannel_->sendNavigationFocusResponse(response, std::move(promise));
    controlServiceChannel_->receive(this->shared_from_this());
}

void AndroidAutoEntity::onVoiceSessionRequest(const aasdk::proto::messages::VoiceSessionRequest& request)
{
    LOG(info) << "Voice session request, type: " << ((request.type() == 1) ? "START" : ((request.type() == 2) ? "STOP" : "UNKNOWN"));

    auto promise = aasdk::channel::SendPromise::defer(strand_);
    promise->then([]() {}, std::bind(&AndroidAutoEntity::onChannelError, this->shared_from_this(), std::placeholders::_1));
    controlServiceChannel_->receive(this->shared_from_this());
}

void AndroidAutoEntity::onPingRequest(const aasdk::proto::messages::PingRequest& request)
{
    LOG(info) << "Ping Request";

    aasdk::proto::messages::PingResponse response;
    response.set_timestamp(request.timestamp());
    auto promise = aasdk::channel::SendPromise::defer(strand_);
    promise->then([]() {}, std::bind(&AndroidAutoEntity::onChannelError, this->shared_from_this(), std::placeholders::_1));
    controlServiceChannel_->sendPingResponse(response, std::move(promise));
    controlServiceChannel_->receive(this->shared_from_this());
}

void AndroidAutoEntity::onPingResponse(const aasdk::proto::messages::PingResponse&)
{
    pinger_->pong();
    controlServiceChannel_->receive(this->shared_from_this());
}

void AndroidAutoEntity::onChannelError(const aasdk::error::Error& e)
{
    LOG(error) << "channel error: " << e.what();
    this->triggerQuit();
}

void AndroidAutoEntity::triggerQuit()
{
    if(eventHandler_ != nullptr)
    {
        eventHandler_->onAndroidAutoQuit();
    }
}

void AndroidAutoEntity::schedulePing()
{
    auto promise = IPinger::Promise::defer(strand_);
    promise->then([this, self = this->shared_from_this()]() {
        this->sendPing();
        this->schedulePing();
    },
    [this, self = this->shared_from_this()](auto error) {
        if(error != aasdk::error::ErrorCode::OPERATION_ABORTED &&
           error != aasdk::error::ErrorCode::OPERATION_IN_PROGRESS)
        {
            LOG(error) << "ping timer exceeded.";
            this->triggerQuit();
        }
    });

    pinger_->ping(std::move(promise));
}

void AndroidAutoEntity::sendPing()
{
    auto promise = aasdk::channel::SendPromise::defer(strand_);
    promise->then([]() {}, std::bind(&AndroidAutoEntity::onChannelError, this->shared_from_this(), std::placeholders::_1));

    aasdk::proto::messages::PingRequest request;
    auto timestamp = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::high_resolution_clock::now().time_since_epoch());
    request.set_timestamp(timestamp.count());
    controlServiceChannel_->sendPingRequest(request, std::move(promise));
}

}
}
