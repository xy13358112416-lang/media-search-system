#include "llm_recommendation.h"
#include <QCoreApplication>
#include <QCommandLineParser>
#include <QSslSocket>
#include <QTextStream>

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("media_server"));
    QCommandLineParser parser;
    parser.addHelpOption();
    parser.addOption({"config", "Server-local configuration file (never pass the key on the command line).", "path"});
    parser.addOption({"send", "Send ONE real, billable API request with synthetic metadata only."});
    parser.process(app);
    const auto config = llm_recommendation::loadConfig(parser.value("config"));
    QTextStream out(stdout);
    out << "key_configured=" << !config.apiKey.isEmpty() << " tls_supported=" << QSslSocket::supportsSsl()
        << " tls_backend=" << QSslSocket::activeBackend() << Qt::endl;
    if (!parser.isSet("send")) return 0;
    if (config.apiKey.isEmpty() || !QSslSocket::supportsSsl()) {
        out << "Not sent: configure the server key and TLS backend first." << Qt::endl;
        return 2;
    }
    llm_recommendation::Client client(config);
    llm_recommendation::Request request;
    request.context = QStringLiteral("音乐收藏：从已授权的示例候选挑选相关内容");
    request.limit = 1;
    MediaRecord music, movie;
    music.fileName = QStringLiteral("示例音乐条目"); music.category = QStringLiteral("音乐");
    movie.fileName = QStringLiteral("示例电影条目"); movie.category = QStringLiteral("电影");
    request.candidates = {{"demo:music", music}, {"demo:movie", movie}};
    client.recommend(request, [&](llm_recommendation::Result result) {
        // Do not print provider payloads, keys, headers or private metadata.
        out << "fallback=" << result.fallback << " valid_items=" << result.items.size() << Qt::endl;
        app.exit(!result.fallback && !result.items.isEmpty() ? 0 : 3);
    });
    return app.exec();
}
