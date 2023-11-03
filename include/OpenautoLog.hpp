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

#pragma once

#include <iostream>
#include <boost/log/core.hpp>
#include <boost/log/trivial.hpp>
#include <boost/log/expressions.hpp>
#include <boost/log/utility/setup/console.hpp>
#include <boost/log/utility/setup/common_attributes.hpp>

#define ADDITIONAL_TAG "OPENAUTO"

#define LOG(severity) BOOST_LOG_TRIVIAL(severity) << "\t[" << gettid() << "][" ADDITIONAL_TAG "][" << \
                        __FILE__ << ":" << __LINE__ << "][" << __FUNCTION__ << "] "

class OpenAutoLog
{
public:
    static void init()
    {
        boost::log::register_simple_formatter_factory<boost::log::trivial::severity_level, char>("Severity");

        boost::log::add_console_log(
            std::cout,
            boost::log::keywords::format = "[%TimeStamp%] [%Severity%] %Message%");

#if 0
        boost::log::core::get()->set_filter(boost::log::trivial::severity >= boost::log::trivial::info);
#endif

        boost::log::add_common_attributes();
    }
};
