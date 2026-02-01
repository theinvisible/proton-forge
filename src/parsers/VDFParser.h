#ifndef VDFPARSER_H
#define VDFPARSER_H

#include <QString>
#include <QVariant>
#include <QMap>

// VDF (Valve Data Format) parser
// Parses files like appmanifest_*.acf, libraryfolders.vdf, config.vdf

class VDFNode {
public:
    VDFNode() = default;

    bool isValue() const { return !m_value.isNull(); }
    bool isObject() const { return m_value.isNull() && !m_children.isEmpty(); }

    QString value() const { return m_value; }
    void setValue(const QString& val) { m_value = val; }

    bool hasChild(const QString& key) const { return m_children.contains(key); }
    VDFNode child(const QString& key) const { return m_children.value(key); }
    void setChild(const QString& key, const VDFNode& node) { m_children[key] = node; }

    QMap<QString, VDFNode> children() const { return m_children; }

    // Convenience accessors
    QString getString(const QString& key, const QString& defaultValue = QString()) const;
    qint64 getInt(const QString& key, qint64 defaultValue = 0) const;

private:
    QString m_value;
    QMap<QString, VDFNode> m_children;
};

class VDFParser {
public:
    VDFParser() = default;

    bool parse(const QString& content);
    bool parseFile(const QString& filePath);

    VDFNode root() const { return m_root; }
    QString errorString() const { return m_error; }

private:
    enum class TokenType {
        String,
        OpenBrace,
        CloseBrace,
        EndOfFile,
        Error
    };

    struct Token {
        TokenType type;
        QString value;
    };

    Token nextToken();
    bool parseNode(VDFNode& node);
    void skipWhitespace();

    QString m_content;
    int m_pos = 0;
    VDFNode m_root;
    QString m_error;
};

#endif // VDFPARSER_H
