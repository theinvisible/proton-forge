#ifndef STEAMARTWORK_H
#define STEAMARTWORK_H

#include <QByteArray>
#include <QString>

#include "utils/SteamPaths.h"

// Where a Steam game's banner (the 460x215 header) comes from.
//
// It used to fall out of the appid — steam/apps/<appid>/header.jpg on the CDN —
// and for older titles it still does. Newer ones are stored under a content
// hash (store_item_assets/steam/apps/<appid>/<hash>/header.jpg) that nothing
// derives from the appid, so that URL answers 404 for them. Two sources know
// the hash: the Steam client, which keeps every banner it has shown in its
// librarycache, and the storefront's appdetails endpoint. resolve() tries them
// in that order and falls back to the old scheme.
//
// File reads only: discovery calls this on a worker thread. The network lookup
// lives in SteamStoreService, which stores the appdetails answer where
// storeHeaderUrl() finds it.
namespace SteamArtwork {

// The banner the Steam client cached: header.jpg or library_header.jpg, in the
// app's directory or in one of its hash subdirectories — the newest wins,
// because Steam leaves the old hash directory behind when a game gets new
// artwork. header_<language>.jpg if there is nothing else. Empty if none.
QString localHeaderPath(const QString& appId,
                        const QString& steamRoot = SteamPaths::steamRoot());

// Where the appdetails answer for an app is cached — shared by the store
// dialog's details panel and the artwork lookup, so one answer serves both.
QString appDetailsCachePath(const QString& appId);

// data.header_image of an appdetails answer; empty for success:false, another
// appid, a missing field or anything that is not JSON.
QString headerFromAppDetails(const QByteArray& json, const QString& appId);

// The banner URL of a cached appdetails answer, if there is one. Kept for
// 90 days: the URL stays good until the artwork changes, and a URL that stops
// working is looked up again (SteamStoreService).
QString storeHeaderUrl(const QString& appId);

// The old CDN scheme.
QString legacyUrl(const QString& appId);

// The appid in a Steam CDN URL (either scheme); empty for anything else.
QString appIdFromUrl(const QString& url);

// Local file (as a file:// URL, which ImageCache reads like any other), else
// the store URL, else the old scheme.
QString resolve(const QString& appId, const QString& steamRoot = SteamPaths::steamRoot());

} // namespace SteamArtwork

#endif // STEAMARTWORK_H
