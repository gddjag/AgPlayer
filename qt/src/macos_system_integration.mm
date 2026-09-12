#include "macos_system_integration.hpp"

#import <AppKit/AppKit.h>
#import <ApplicationServices/ApplicationServices.h>
#import <Carbon/Carbon.h>
#import <CoreServices/CoreServices.h>
#import <Foundation/Foundation.h>
#import <ServiceManagement/ServiceManagement.h>
#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>

#include <QSettings>

#include <map>
#include <utility>

namespace {

struct HotkeyRegistration {
    EventHotKeyRef carbonRef = nullptr;
    unsigned int mediaKey = 0;
    void* context = nullptr;
    agplayer::qt::macos::HotkeyCallback callback = nullptr;
};

std::map<int, HotkeyRegistration> carbonHotkeys;
std::map<int, HotkeyRegistration> mediaHotkeys;
EventHandlerRef carbonHandler = nullptr;
CFMachPortRef mediaTap = nullptr;
CFRunLoopSourceRef mediaTapSource = nullptr;

QString fromNSString(NSString* value)
{
    return value == nil ? QString{} : QString::fromUtf8(value.UTF8String);
}

NSString* toNSString(const QString& value)
{
    return [NSString stringWithUTF8String:value.toUtf8().constData()];
}

QString bundleIdentifier()
{
    return fromNSString(NSBundle.mainBundle.bundleIdentifier);
}

bool bundleDeclaresType(UTType* type, const QString& extension)
{
    NSString* requested = toNSString(extension.toLower());
    NSArray* documentTypes =
        [NSBundle.mainBundle objectForInfoDictionaryKey:@"CFBundleDocumentTypes"];
    for (id documentType in documentTypes) {
        if (![documentType isKindOfClass:NSDictionary.class]) continue;
        NSArray* contentTypes =
            [(NSDictionary*)documentType objectForKey:@"LSItemContentTypes"];
        for (id declared in contentTypes) {
            if (![declared isKindOfClass:NSString.class]) continue;
            UTType* declaredType = [UTType typeWithIdentifier:(NSString*)declared];
            if (declaredType != nil && [type conformsToType:declaredType]) return true;
        }
        NSArray* extensions =
            [(NSDictionary*)documentType objectForKey:@"CFBundleTypeExtensions"];
        for (id declared in extensions) {
            if ([declared isKindOfClass:NSString.class]
                && [((NSString*)declared).lowercaseString isEqualToString:requested]) {
                return true;
            }
        }
    }
    return false;
}

UTType* typeForExtension(const QString& extension)
{
    return [UTType typeWithFilenameExtension:toNSString(extension)];
}

QString associationSettingsKey(const QString& extension)
{
    return QStringLiteral("macos/file-associations/%1/previous-handler")
        .arg(extension.toLower());
}

QString currentHandler(UTType* type)
{
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
    CFStringRef handler = LSCopyDefaultRoleHandlerForContentType(
        (__bridge CFStringRef)type.identifier, kLSRolesAll);
#pragma clang diagnostic pop
    if (handler == nullptr) return {};
    NSString* const value = (__bridge NSString*)handler;
    const QString result = fromNSString(value);
    CFRelease(handler);
    return result;
}

bool setDefaultHandler(UTType* type, const QString& handler, QString* error)
{
    if (handler.isEmpty()) {
        if (error != nullptr) *error = QStringLiteral("没有可恢复的原默认应用");
        return false;
    }
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
    const OSStatus status = LSSetDefaultRoleHandlerForContentType(
        (__bridge CFStringRef)type.identifier, kLSRolesAll,
        (__bridge CFStringRef)toNSString(handler));
#pragma clang diagnostic pop
    if (status == noErr) return true;
    if (error != nullptr) {
        *error = QStringLiteral("LaunchServices 设置默认应用失败（%1）")
            .arg(static_cast<qint64>(status));
    }
    return false;
}

void invoke(const HotkeyRegistration& registration, int id)
{
    if (registration.callback != nullptr) registration.callback(registration.context, id);
}

OSStatus carbonHotkeyHandler(EventHandlerCallRef, EventRef event, void*)
{
    EventHotKeyID hotkeyId{};
    if (GetEventParameter(event, kEventParamDirectObject, typeEventHotKeyID,
                          nullptr, sizeof(hotkeyId), nullptr, &hotkeyId) != noErr) {
        return eventNotHandledErr;
    }
    const auto it = carbonHotkeys.find(static_cast<int>(hotkeyId.id));
    if (it == carbonHotkeys.end()) return eventNotHandledErr;
    invoke(it->second, static_cast<int>(hotkeyId.id));
    return noErr;
}

CGEventRef mediaEventCallback(CGEventTapProxy, CGEventType type,
                              CGEventRef event, void*)
{
    if (type == kCGEventTapDisabledByTimeout || type == kCGEventTapDisabledByUserInput) {
        if (mediaTap != nullptr) CGEventTapEnable(mediaTap, true);
        return event;
    }
    NSEvent* const nativeEvent = [NSEvent eventWithCGEvent:event];
    if (nativeEvent == nil || nativeEvent.type != NSEventTypeSystemDefined
        || nativeEvent.subtype != 8) {
        return event;
    }
    const NSInteger data = nativeEvent.data1;
    const unsigned int key = static_cast<unsigned int>((data >> 16) & 0xffff);
    const unsigned int state = static_cast<unsigned int>((data >> 8) & 0xff);
    if (state != 0x0aU) return event; // only key-down system media events
    for (const auto& [id, registration] : mediaHotkeys) {
        if (registration.mediaKey == key) {
            invoke(registration, id);
        }
    }
    return event;
}

bool ensureMediaTap(QString* error)
{
    if (mediaTap != nullptr) return true;
    if (!CGPreflightListenEventAccess()) {
        CGRequestListenEventAccess();
        if (error != nullptr) {
            *error = QStringLiteral("媒体键需要“输入监控”权限；请在系统设置中允许 AgPlayer 后重试");
        }
        return false;
    }
    mediaTap = CGEventTapCreate(kCGSessionEventTap, kCGHeadInsertEventTap,
                                kCGEventTapOptionListenOnly,
                                 CGEventMaskBit(static_cast<CGEventType>(NSEventTypeSystemDefined)),
                                mediaEventCallback, nullptr);
    if (mediaTap == nullptr) {
        if (error != nullptr) *error = QStringLiteral("无法创建系统媒体键监听");
        return false;
    }
    mediaTapSource = CFMachPortCreateRunLoopSource(kCFAllocatorDefault, mediaTap, 0);
    if (mediaTapSource == nullptr) {
        CFRelease(mediaTap);
        mediaTap = nullptr;
        if (error != nullptr) *error = QStringLiteral("无法创建系统媒体键运行循环源");
        return false;
    }
    CFRunLoopAddSource(CFRunLoopGetMain(), mediaTapSource, kCFRunLoopCommonModes);
    CGEventTapEnable(mediaTap, true);
    return true;
}

void disposeMediaTapIfUnused()
{
    if (!mediaHotkeys.empty() || mediaTap == nullptr) return;
    CFRunLoopRemoveSource(CFRunLoopGetMain(), mediaTapSource, kCFRunLoopCommonModes);
    CFRelease(mediaTapSource);
    CFRelease(mediaTap);
    mediaTapSource = nullptr;
    mediaTap = nullptr;
}

} // namespace

bool agplayer::qt::macos::loginItemEnabled(QString* error)
{
    @autoreleasepool {
        if (@available(macOS 13.0, *)) {
            const SMAppServiceStatus status = SMAppService.mainAppService.status;
            if (status == SMAppServiceStatusEnabled) return true;
            if (error != nullptr && status == SMAppServiceStatusRequiresApproval) {
                *error = QStringLiteral("登录项等待用户在系统设置中批准");
            }
            return false;
        }
        if (error != nullptr) *error = QStringLiteral("登录项需要 macOS 13 或更高版本");
        return false;
    }
}

bool agplayer::qt::macos::setLoginItemEnabled(bool enabled, QString* error)
{
    @autoreleasepool {
        if (@available(macOS 13.0, *)) {
            SMAppService* const service = SMAppService.mainAppService;
            NSError* nativeError = nil;
            const bool operationOk = enabled
                ? [service registerAndReturnError:&nativeError]
                : [service unregisterAndReturnError:&nativeError];
            if (!operationOk) {
                if (error != nullptr) *error = fromNSString(nativeError.localizedDescription);
                return false;
            }
            const bool actual = service.status == SMAppServiceStatusEnabled;
            if (actual != enabled && error != nullptr) {
                *error = enabled
                    ? QStringLiteral("登录项等待用户在系统设置中批准")
                    : QStringLiteral("登录项状态尚未同步为已关闭");
            }
            return actual == enabled;
        } else {
            if (error != nullptr) *error = QStringLiteral("登录项需要 macOS 13 或更高版本");
            return false;
        }
    }
}

bool agplayer::qt::macos::setFileAssociation(const QString& extension, QString* error)
{
    @autoreleasepool {
        const QString normalized = extension.trimmed().toLower();
        UTType* const type = typeForExtension(normalized);
        if (type == nil) {
            if (error != nullptr) *error = QStringLiteral("无法解析 .%1 的统一类型标识").arg(normalized);
            return false;
        }
        if (!bundleDeclaresType(type, normalized)) {
            if (error != nullptr) {
                *error = QStringLiteral("Info.plist 未声明 .%1 文件类型").arg(normalized);
            }
            return false;
        }
        const QString bundleId = bundleIdentifier();
        if (bundleId.isEmpty()) {
            if (error != nullptr) *error = QStringLiteral("文件关联需要已打包的 macOS 应用");
            return false;
        }
        const QString current = currentHandler(type);
        if (current == bundleId) return true;
        QSettings settings;
        const QString key = associationSettingsKey(normalized);
        if (!settings.contains(key)) settings.setValue(key, current);
        if (!setDefaultHandler(type, bundleId, error)) return false;
        settings.sync();
        return true;
    }
}

bool agplayer::qt::macos::restoreFileAssociation(const QString& extension, QString* error)
{
    @autoreleasepool {
        const QString normalized = extension.trimmed().toLower();
        const QString bundleId = bundleIdentifier();
        UTType* const type = typeForExtension(normalized);
        if (bundleId.isEmpty() || type == nil) return true;
        QSettings settings;
        const QString key = associationSettingsKey(normalized);
        if (!settings.contains(key)) return true;
        const QString current = currentHandler(type);
        const QString previous = settings.value(key).toString();
        if (current == bundleId) {
            if (previous.isEmpty()) {
                if (error != nullptr) {
                    *error = QStringLiteral("没有可恢复的原默认应用；请在 Finder 中选择其他应用");
                }
                return false;
            }
            if (!setDefaultHandler(type, previous, error)) return false;
        }
        // Do not override a default the user chose after enabling AgPlayer.
        settings.remove(key);
        settings.sync();
        return true;
    }
}

bool agplayer::qt::macos::isFileAssociationActive(const QString& extension)
{
    @autoreleasepool {
        UTType* const type = typeForExtension(extension.trimmed().toLower());
        const QString bundleId = bundleIdentifier();
        return type != nil && !bundleId.isEmpty() && currentHandler(type) == bundleId;
    }
}

QStringList agplayer::qt::macos::managedFileAssociations()
{
    QSettings settings;
    settings.beginGroup(QStringLiteral("macos/file-associations"));
    const QStringList extensions = settings.childGroups();
    settings.endGroup();
    return extensions;
}

bool agplayer::qt::macos::registerHotkey(int id, unsigned int modifiers,
                                         unsigned int key, void* context,
                                         HotkeyCallback callback, QString* error)
{
    if (carbonHandler == nullptr) {
        EventTypeSpec spec{kEventClassKeyboard, kEventHotKeyPressed};
        if (InstallEventHandler(GetApplicationEventTarget(), carbonHotkeyHandler,
                                1, &spec, nullptr, &carbonHandler) != noErr) {
            if (error != nullptr) *error = QStringLiteral("无法安装 macOS 全局快捷键处理器");
            return false;
        }
    }
    EventHotKeyID hotkeyId{};
    hotkeyId.signature = 0x4147504bU;
    hotkeyId.id = static_cast<UInt32>(id);
    EventHotKeyRef reference = nullptr;
    const OSStatus status = RegisterEventHotKey(static_cast<UInt32>(key),
                                                static_cast<UInt32>(modifiers),
                                                hotkeyId, GetApplicationEventTarget(),
                                                0, &reference);
    if (status != noErr) {
        if (error != nullptr) {
            *error = QStringLiteral("macOS 全局快捷键注册失败（%1），可能已被其他应用占用")
                .arg(static_cast<qint64>(status));
        }
        return false;
    }
    carbonHotkeys[id] = HotkeyRegistration{reference, 0, context, callback};
    return true;
}

bool agplayer::qt::macos::registerMediaHotkey(int id, unsigned int mediaKey,
                                              void* context, HotkeyCallback callback,
                                              QString* error)
{
    if (!ensureMediaTap(error)) return false;
    mediaHotkeys[id] = HotkeyRegistration{nullptr, mediaKey, context, callback};
    return true;
}

void agplayer::qt::macos::unregisterHotkey(int id)
{
    const auto carbon = carbonHotkeys.find(id);
    if (carbon != carbonHotkeys.end()) {
        UnregisterEventHotKey(carbon->second.carbonRef);
        carbonHotkeys.erase(carbon);
    }
    mediaHotkeys.erase(id);
    disposeMediaTapIfUnused();
}
