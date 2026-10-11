#include "game/platform.hpp"

#import <Foundation/Foundation.h>

namespace opena2::game {

bool moveToTrash(const std::filesystem::path& path)
{
    @autoreleasepool {
        NSString* text = [NSString stringWithUTF8String:reinterpret_cast<const char*>(path.u8string().c_str())];
        NSURL* url = [NSURL fileURLWithPath:text];
        return [[NSFileManager defaultManager] trashItemAtURL:url resultingItemURL:nil error:nil] == YES;
    }
}

} // namespace opena2::game
