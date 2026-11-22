// >>> DEFENSE-NOTE | 头文件导读｜人物Search/Fetch接口
// Request由可信人物配置构造，approvedSite是精确域名，identityClue是公开消歧线索；Key只从服务端调用参数传入。
// 读法：struct说明传哪些数据；class/函数声明说明谁可调用；实际算法去同名.cpp或引用的实现文件。
// <<< DEFENSE-NOTE | 本段讲解结束
#pragma once

#include "care_contract.h"

class QNetworkAccessManager;
namespace person_store { class Store; }

namespace deepseek_person_news {
struct Request {
    QString personId;
    QString displayName;
    QString approvedSite; // Exact verified publisher hostname, without scheme or path.
    QString identityClue; // A public work title or other human-confirmed disambiguator.
};

// Namespaces this source in the existing cache independently of RSS/Atom.
care_contract::Result<QUrl> cacheSource(const Request& request);
bool isSearchSource(const QUrl& source);

// Returns public profile links found by Search and checked with Fetch.
void findPeople(QNetworkAccessManager& network, const QString& name, const QByteArray& apiKey,
                care_contract::Callback<QList<care_contract::PersonWebSource>> done);

// Operates on an already registered source on the store's thread.
void refresh(person_store::Store& store, QNetworkAccessManager& network, const Request& request,
             const QByteArray& apiKey, care_contract::StatusCallback done);
} // namespace deepseek_person_news
