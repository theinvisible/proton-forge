#include "VDFParser.h"
#include <QFile>
#include <QTextStream>

QString VDFNode::getString(const QString& key, const QString& defaultValue) const
{
    if (m_children.contains(key) && m_children[key].isValue()) {
        return m_children[key].value();
    }
    return defaultValue;
}

qint64 VDFNode::getInt(const QString& key, qint64 defaultValue) const
{
    if (m_children.contains(key) && m_children[key].isValue()) {
        bool ok;
        qint64 val = m_children[key].value().toLongLong(&ok);
        return ok ? val : defaultValue;
    }
    return defaultValue;
}

bool VDFParser::parseFile(const QString& filePath)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        m_error = "Cannot open file: " + filePath;
        return false;
    }

    QTextStream stream(&file);
    QString content = stream.readAll();
    file.close();

    return parse(content);
}

bool VDFParser::parse(const QString& content)
{
    m_content = content;
    m_pos = 0;
    m_root = VDFNode();
    m_error.clear();

    skipWhitespace();

    // Parse root-level key-value pairs
    while (m_pos < m_content.length()) {
        Token keyToken = nextToken();

        if (keyToken.type == TokenType::EndOfFile) {
            break;
        }

        if (keyToken.type != TokenType::String) {
            m_error = "Expected string key at position " + QString::number(m_pos);
            return false;
        }

        skipWhitespace();
        Token valueToken = nextToken();

        if (valueToken.type == TokenType::OpenBrace) {
            // Nested object
            VDFNode childNode;
            if (!parseNode(childNode)) {
                return false;
            }
            m_root.setChild(keyToken.value, childNode);
        } else if (valueToken.type == TokenType::String) {
            // Simple key-value
            VDFNode valueNode;
            valueNode.setValue(valueToken.value);
            m_root.setChild(keyToken.value, valueNode);
        } else {
            m_error = "Expected value or '{' at position " + QString::number(m_pos);
            return false;
        }

        skipWhitespace();
    }

    return true;
}

bool VDFParser::parseNode(VDFNode& node)
{
    skipWhitespace();

    while (m_pos < m_content.length()) {
        Token keyToken = nextToken();

        if (keyToken.type == TokenType::CloseBrace) {
            return true;
        }

        if (keyToken.type == TokenType::EndOfFile) {
            m_error = "Unexpected end of file, expected '}'";
            return false;
        }

        if (keyToken.type != TokenType::String) {
            m_error = "Expected string key at position " + QString::number(m_pos);
            return false;
        }

        skipWhitespace();
        Token valueToken = nextToken();

        if (valueToken.type == TokenType::OpenBrace) {
            // Nested object
            VDFNode childNode;
            if (!parseNode(childNode)) {
                return false;
            }
            node.setChild(keyToken.value, childNode);
        } else if (valueToken.type == TokenType::String) {
            // Simple key-value
            VDFNode valueNode;
            valueNode.setValue(valueToken.value);
            node.setChild(keyToken.value, valueNode);
        } else {
            m_error = "Expected value or '{' at position " + QString::number(m_pos);
            return false;
        }

        skipWhitespace();
    }

    m_error = "Unexpected end of file, expected '}'";
    return false;
}

void VDFParser::skipWhitespace()
{
    while (m_pos < m_content.length()) {
        QChar c = m_content[m_pos];

        if (c.isSpace()) {
            m_pos++;
            continue;
        }

        // Skip // comments
        if (c == '/' && m_pos + 1 < m_content.length() && m_content[m_pos + 1] == '/') {
            while (m_pos < m_content.length() && m_content[m_pos] != '\n') {
                m_pos++;
            }
            continue;
        }

        break;
    }
}

VDFParser::Token VDFParser::nextToken()
{
    skipWhitespace();

    if (m_pos >= m_content.length()) {
        return {TokenType::EndOfFile, QString()};
    }

    QChar c = m_content[m_pos];

    if (c == '{') {
        m_pos++;
        return {TokenType::OpenBrace, "{"};
    }

    if (c == '}') {
        m_pos++;
        return {TokenType::CloseBrace, "}"};
    }

    if (c == '"') {
        // Quoted string
        m_pos++;
        QString value;
        while (m_pos < m_content.length()) {
            c = m_content[m_pos];
            if (c == '"') {
                m_pos++;
                return {TokenType::String, value};
            }
            if (c == '\\' && m_pos + 1 < m_content.length()) {
                m_pos++;
                QChar escaped = m_content[m_pos];
                if (escaped == 'n') {
                    value += '\n';
                } else if (escaped == 't') {
                    value += '\t';
                } else if (escaped == '\\') {
                    value += '\\';
                } else if (escaped == '"') {
                    value += '"';
                } else {
                    value += escaped;
                }
                m_pos++;
                continue;
            }
            value += c;
            m_pos++;
        }
        m_error = "Unterminated string";
        return {TokenType::Error, QString()};
    }

    // Unquoted string (some VDF files use unquoted keys)
    if (c.isLetterOrNumber() || c == '_') {
        QString value;
        while (m_pos < m_content.length()) {
            c = m_content[m_pos];
            if (c.isLetterOrNumber() || c == '_' || c == '-' || c == '.') {
                value += c;
                m_pos++;
            } else {
                break;
            }
        }
        return {TokenType::String, value};
    }

    m_error = QString("Unexpected character '%1' at position %2").arg(c).arg(m_pos);
    return {TokenType::Error, QString()};
}
