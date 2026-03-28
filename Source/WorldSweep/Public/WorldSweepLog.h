// Copyright © ToaGames. All Rights Reserved.

#pragma once

#include "Logging/LogMacros.h"

WORLDSWEEP_API DECLARE_LOG_CATEGORY_EXTERN(LogWorldSweep, Log, All);

/**
 * WS_NOTIFY_ERROR / WS_NOTIFY_WARNING
 *
 * Drop-in replacements for UE_LOG that also fire an editor toast notification
 * (FSlateNotificationManager) so misconfiguration issues are impossible to miss.
 * In commandlets only the log line is emitted. Outside WITH_EDITOR both macros
 * reduce to plain UE_LOG calls with no Slate dependency.
 *
 * The log line automatically prefixes [ClassName::FunctionName] via __FUNCTION__
 * for programmer traceability. The toast uses the raw Format so it stays readable
 * for designers and artists.
 *
 * Usage (identical to UE_LOG minus the verbosity arg):
 *   WS_NOTIFY_ERROR(TEXT("Batch '%s' has no scripts."), *Name);
 *   WS_NOTIFY_WARNING(TEXT("EventFlags = 0 on '%s'."), *ClassName);
 */
#if WITH_EDITOR
#include "Framework/Notifications/NotificationManager.h"
#include "Widgets/Notifications/SNotificationList.h"

#define WS_NOTIFY_ERROR(Format, ...)                                                                    \
    do {                                                                                                \
        UE_LOG(LogWorldSweep, Error, TEXT("[%hs] ") Format, __FUNCTION__, ##__VA_ARGS__);               \
        if (!IsRunningCommandlet())                                                                     \
        {                                                                                               \
            FNotificationInfo _WsInfo(FText::FromString(FString::Printf(Format, ##__VA_ARGS__)));       \
            _WsInfo.bFireAndForget = true;                                                              \
            _WsInfo.ExpireDuration = 8.0f;                                                              \
            _WsInfo.bUseLargeFont  = false;                                                             \
            TSharedPtr<SNotificationItem> _WsN = FSlateNotificationManager::Get().AddNotification(_WsInfo); \
            if (_WsN.IsValid()) { _WsN->SetCompletionState(SNotificationItem::CS_Fail); }               \
        }                                                                                               \
    } while (0)

#define WS_NOTIFY_WARNING(Format, ...)                                                                  \
    do {                                                                                                \
        UE_LOG(LogWorldSweep, Warning, TEXT("[%hs] ") Format, __FUNCTION__, ##__VA_ARGS__);             \
        if (!IsRunningCommandlet())                                                                     \
        {                                                                                               \
            FNotificationInfo _WsInfo(FText::FromString(FString::Printf(Format, ##__VA_ARGS__)));       \
            _WsInfo.bFireAndForget = true;                                                              \
            _WsInfo.ExpireDuration = 6.0f;                                                              \
            _WsInfo.bUseLargeFont  = false;                                                             \
            FSlateNotificationManager::Get().AddNotification(_WsInfo);                                  \
        }                                                                                               \
    } while (0)

#else
#define WS_NOTIFY_ERROR(Format, ...)   UE_LOG(LogWorldSweep, Error,   TEXT("[%hs] ") Format, __FUNCTION__, ##__VA_ARGS__)
#define WS_NOTIFY_WARNING(Format, ...) UE_LOG(LogWorldSweep, Warning, TEXT("[%hs] ") Format, __FUNCTION__, ##__VA_ARGS__)
#endif
