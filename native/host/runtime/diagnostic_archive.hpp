#pragma once
#include "common.hpp"
#include <QDataStream>
#include <QSaveFile>

namespace okww::runtime {
// ZIP32 is sufficient for the bounded diagnostic files. Screenshots are
// already compressed, while reports and logs benefit from raw DEFLATE.
inline QString writeDiagnosticArchive(const QDir &folder) {
    const QFileInfo directory(folder.absolutePath());
    const auto root = directory.canonicalFilePath();
    const auto name = directory.fileName();
    static const QRegularExpression idPattern("^\\d{8}-\\d{6}-\\d{3}-[a-f0-9]{8}$");
    if (root.isEmpty() || !idPattern.match(name).hasMatch())
        fail("诊断目录无效");
    const auto path = folder.absoluteFilePath("../OKWW-diagnostic-" + name + ".zip");
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        fail(file.errorString());
    QDataStream zip(&file);
    zip.setByteOrder(QDataStream::LittleEndian);
    const auto now = QDateTime::currentDateTime();
    const quint16 date = quint16((qBound(1980, now.date().year(), 2107) - 1980) << 9
                                | now.date().month() << 5 | now.date().day());
    const quint16 time = quint16(now.time().hour() << 11 | now.time().minute() << 5 | now.time().second() / 2);
    struct Entry {
        QByteArray name;
        quint32 offset, crc, size, compressedSize;
        quint16 method;
    };
    QList<Entry> entries;
    const auto write = [&](const QByteArray &bytes) {
        if (file.write(bytes) != bytes.size())
            fail(file.errorString());
    };

    QStringList files{"report.json", "native.log", "workshop.log", "screenshot.png"};
    for (const auto &step : {"steps/01/", "steps/02/", "steps/03/"}) {
        files.append(QString(step) + "report.json");
        files.append(QString(step) + "screenshot.png");
    }
    for (const auto &relative : files) {
        const QFileInfo info(folder.filePath(relative));
        if (!info.exists() && relative != "report.json")
            continue;
        const bool image = relative.endsWith(".png");
        const auto limit = image ? 32 * 1024 * 1024 : 2 * 1024 * 1024;
        if (!info.isFile() || info.isSymLink() || info.size() > limit ||
            info.canonicalFilePath().compare(root + '/' + relative, Qt::CaseInsensitive) != 0)
            fail("无法打包诊断文件 " + relative);
        QFile input(info.filePath());
        if (!input.open(QIODevice::ReadOnly) || input.size() < 0 || input.size() > limit)
            fail(input.errorString());
        Entry entry{relative.toUtf8(), quint32(file.pos()), 0, quint32(input.size()), 0, quint16(image ? 0 : 8)};
        // Use a data descriptor so PNGs can be copied in small blocks.
        constexpr quint16 flags = 1 << 3;
        zip << quint32(0x04034b50) << quint16(20) << flags << entry.method << time << date
            << quint32(0) << quint32(0) << quint32(0) << quint16(entry.name.size()) << quint16(0);
        write(entry.name);
        static constexpr auto crcTable = [] {
            std::array<quint32, 256> table{};
            for (quint32 index = 0; index < table.size(); ++index) {
                auto value = index;
                for (int bit = 0; bit < 8; ++bit)
                    value = (value >> 1) ^ (value & 1 ? 0xedb88320u : 0);
                table[index] = value;
            }
            return table;
        }();
        quint32 crc = 0xffffffffu;
        quint32 remaining = entry.size;
        const auto start = file.pos();
        do {
            const auto bytes = input.read(image ? qMin(remaining, quint32(64 * 1024)) : remaining);
            if (input.error() != QFileDevice::NoError || (bytes.isEmpty() && remaining != 0))
                fail("无法读取诊断文件 " + relative);
            for (const unsigned char byte : bytes)
                crc = (crc >> 8) ^ crcTable[(crc ^ byte) & 0xff];
            if (image)
                write(bytes);
            else {
                const auto zlib = qCompress(bytes);
                if (bytes.isEmpty()) {
                    // qCompress emits only its length prefix for an empty input.
                    write(QByteArray::fromHex("0300"));
                } else {
                    if (zlib.size() <= 10)
                        fail("无法压缩诊断文件 " + relative);
                    write(zlib.mid(6, zlib.size() - 10));
                }
            }
            remaining -= quint32(bytes.size());
        } while (remaining != 0);
        entry.crc = crc ^ 0xffffffffu;
        entry.compressedSize = quint32(file.pos() - start);
        zip << quint32(0x08074b50) << entry.crc << entry.compressedSize << entry.size;
        entries.append(entry);
    }
    const auto offset = quint32(file.pos());
    for (const auto &entry : entries) {
        zip << quint32(0x02014b50) << quint16(20) << quint16(20) << quint16(1 << 3) << entry.method << time << date
            << entry.crc << entry.compressedSize << entry.size << quint16(entry.name.size()) << quint16(0)
            << quint16(0) << quint16(0) << quint16(0) << quint32(0) << entry.offset;
        write(entry.name);
    }
    const auto size = quint32(file.pos()) - offset;
    zip << quint32(0x06054b50) << quint16(0) << quint16(0) << quint16(entries.size()) << quint16(entries.size())
        << size << offset << quint16(0);
    if (zip.status() != QDataStream::Ok || !file.commit())
        fail("无法保存诊断 ZIP：" + file.errorString());
    return QDir::cleanPath(path);
}
} // namespace okww::runtime
