// HTTP upload and JSON parsing via Foundation (NSURLSession,
// NSJSONSerialization): the one Objective-C++ file, so the rest of the
// project stays plain C++ with no third-party networking/JSON libraries.

#include "transcriber.hh"

#import <Foundation/Foundation.h>

#include <format>

namespace supermoan {

namespace {

void append_str(NSMutableData* data, NSString* s) {
    [data appendData:[s dataUsingEncoding:NSUTF8StringEncoding]];
}

} // namespace

std::expected<std::string, std::string> transcribe(const std::string& wav_path,
                                                   const std::string& model,
                                                   const std::string& prompt,
                                                   const std::string& api_key) {
    @autoreleasepool {
        NSData* wav = [NSData dataWithContentsOfFile:@(wav_path.c_str())];
        if (!wav) return std::unexpected(std::format("cannot read {}", wav_path));

        NSString* boundary = [NSString stringWithFormat:@"supermoan-%08x%08x",
                                                        arc4random(), arc4random()];
        NSMutableData* body = [NSMutableData data];
        append_str(body, [NSString stringWithFormat:
            @"--%@\r\nContent-Disposition: form-data; name=\"file\"; "
            @"filename=\"audio.wav\"\r\nContent-Type: audio/wav\r\n\r\n", boundary]);
        [body appendData:wav];
        append_str(body, [NSString stringWithFormat:
            @"\r\n--%@\r\nContent-Disposition: form-data; name=\"model\"\r\n\r\n%s",
            boundary, model.c_str()]);
        append_str(body, [NSString stringWithFormat:
            @"\r\n--%@\r\nContent-Disposition: form-data; name=\"prompt\"\r\n\r\n%s",
            boundary, prompt.c_str()]);
        append_str(body, [NSString stringWithFormat:@"\r\n--%@--\r\n", boundary]);

        NSMutableURLRequest* req = [NSMutableURLRequest requestWithURL:
            [NSURL URLWithString:@"https://api.groq.com/openai/v1/audio/transcriptions"]];
        req.HTTPMethod = @"POST";
        req.HTTPBody = body;
        req.timeoutInterval = 120;
        [req setValue:[NSString stringWithFormat:@"Bearer %s", api_key.c_str()]
            forHTTPHeaderField:@"Authorization"];
        [req setValue:[NSString stringWithFormat:@"multipart/form-data; boundary=%@", boundary]
            forHTTPHeaderField:@"Content-Type"];

        __block NSData* respData = nil;
        __block NSHTTPURLResponse* resp = nil;
        __block NSError* error = nil;
        dispatch_semaphore_t done = dispatch_semaphore_create(0);

        NSURLSessionDataTask* task = [[NSURLSession sharedSession]
            dataTaskWithRequest:req
              completionHandler:^(NSData* d, NSURLResponse* r, NSError* e) {
                respData = d;
                resp = (NSHTTPURLResponse*)r;
                error = e;
                dispatch_semaphore_signal(done);
              }];
        [task resume];

        if (dispatch_semaphore_wait(done, dispatch_time(DISPATCH_TIME_NOW, 125 * NSEC_PER_SEC))) {
            [task cancel];
            return std::unexpected("request timed out");
        }
        if (error)
            return std::unexpected(std::string{error.localizedDescription.UTF8String});

        NSString* bodyText = respData
            ? [[NSString alloc] initWithData:respData encoding:NSUTF8StringEncoding]
            : @"";
        if (resp.statusCode != 200)
            return std::unexpected(std::format("HTTP {}: {}", resp.statusCode,
                                               bodyText.UTF8String ?: ""));

        NSError* jsonError = nil;
        NSDictionary* json = [NSJSONSerialization JSONObjectWithData:respData
                                                             options:0
                                                               error:&jsonError];
        NSString* text = [json isKindOfClass:NSDictionary.class] ? json[@"text"] : nil;
        if (![text isKindOfClass:NSString.class])
            return std::unexpected(std::format("unexpected response: {}",
                                               bodyText.UTF8String ?: ""));

        std::string result = text.UTF8String;
        if (result.starts_with(' ')) result.erase(0, 1); // Whisper's leading space
        return result;
    }
}

} // namespace supermoan
