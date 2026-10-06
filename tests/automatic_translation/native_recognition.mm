#import <Foundation/Foundation.h>
#import <NaturalLanguage/NLLanguageRecognizer.h>
#include <iostream>
int main() {
    @autoreleasepool {
        NSArray<NSDictionary *> *samples = @[
            @{ @"language": @"ru", @"text": @"Сегодня мы обсуждаем последние новости и события в городе." },
            @{ @"language": @"ja", @"text": @"今日は日本語のメッセージを確認しています。" },
            @{ @"language": @"en", @"text": @"This is an English message about the latest news." },
            @{ @"language": @"uk", @"text": @"Сьогодні ми обговорюємо останні новини та події в місті." },
            @{ @"language": @"ru", @"text": @"Доброе утро!" },
            @{ @"language": @"ja", @"text": @"おはようございます！" },
        ];
        for (NSDictionary *sample in samples) {
            auto recognizer = [[NLLanguageRecognizer alloc] init];
            [recognizer processString:sample[@"text"]];
            auto hypotheses = [recognizer languageHypothesesWithMaximum:3];
            double maximum = 0;
            NSString *language = nil;
            for (NSString *candidate in hypotheses) {
                const auto probability = [hypotheses[candidate] floatValue];
                if (probability > maximum) { maximum = probability; language = candidate; }
            }
            std::cout << [sample[@"language"] UTF8String] << " -> "
                << (language ? [language UTF8String] : "unknown") << "\n";
            if (![language isEqualToString:sample[@"language"]]) return 1;
        }
    }
}
