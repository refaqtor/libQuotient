/******************************************************************************
 * Copyright (C) 2017 Kitsune Ral <kitsune-ral@users.sf.net>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 */

#include "avatar.h"

#include "jobs/mediathumbnailjob.h"
#include "events/eventcontent.h"
#include "connection.h"

#include <QtGui/QPainter>

using namespace QMatrixClient;

class Avatar::Private
{
    public:
        Private(Connection* c, QIcon di) : _connection(c), _defaultIcon(di) { }
        QImage get(QSize size, Avatar::notifier_t notifier);

        Connection* _connection;
        const QIcon _defaultIcon;
        QUrl _url;
        QImage _originalImage;

        std::vector<QPair<QSize, QImage>> _scaledImages;

        QSize _requestedSize;
        bool _valid = false;
        MediaThumbnailJob* _ongoingRequest = nullptr;
        std::vector<notifier_t> notifiers;
};

Avatar::Avatar(Connection* connection, QIcon defaultIcon)
    : d(new Private { connection, std::move(defaultIcon) })
{ }

Avatar::~Avatar() = default;

QImage Avatar::get(int dimension, Avatar::notifier_t notifier)
{
    return d->get({dimension, dimension}, notifier);
}

QImage Avatar::get(int width, int height, Avatar::notifier_t notifier)
{
    return d->get({width, height}, notifier);
}

QImage Avatar::Private::get(QSize size, Avatar::notifier_t notifier)
{
    // FIXME: Alternating between longer-width and longer-height requests
    // is a sure way to trick the below code into constantly getting another
    // image from the server because the existing one is alleged unsatisfactory.
    // This is plain abuse by the client, though; so not critical for now.
    if( ( !(_valid || _ongoingRequest)
          || size.width() > _requestedSize.width()
          || size.height() > _requestedSize.height() ) && _url.isValid() )
    {
        qCDebug(MAIN) << "Getting avatar from" << _url.toString();
        _requestedSize = size;
        if (_ongoingRequest)
            _ongoingRequest->abandon();
        notifiers.emplace_back(std::move(notifier));
        _ongoingRequest = _connection->getThumbnail(_url, size);
        _ongoingRequest->connect( _ongoingRequest, &MediaThumbnailJob::finished,
                                 _connection, [=]() {
            if (_ongoingRequest->status().good())
            {
                _valid = true;
                _originalImage =
                        _ongoingRequest->scaledThumbnail(_requestedSize);
                _scaledImages.clear();
                for (auto n: notifiers)
                    n();
            }
            _ongoingRequest = nullptr;
        });
    }

    if( _originalImage.isNull() )
    {
        if (_defaultIcon.isNull())
            return _originalImage;

        QPainter p { &_originalImage };
        _defaultIcon.paint(&p, { QPoint(), _defaultIcon.actualSize(size) });
    }

    for (auto p: _scaledImages)
        if (p.first == size)
            return p.second;
    auto result = _originalImage.scaled(size,
                    Qt::KeepAspectRatio, Qt::SmoothTransformation);
    _scaledImages.emplace_back(size, result);
    return result;
}

QUrl Avatar::url() const { return d->_url; }

bool Avatar::updateUrl(const QUrl& newUrl)
{
    if (newUrl == d->_url)
        return false;

    d->_url = newUrl;
    d->_valid = false;
    return true;
}

