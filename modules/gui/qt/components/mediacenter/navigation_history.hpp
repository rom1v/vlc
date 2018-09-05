#ifndef NAVIGATION_HISTORY_HPP
#define NAVIGATION_HISTORY_HPP

#include <QObject>
#include <QList>
#include <QtQml/QQmlPropertyMap>

class NavigationHistory : public QObject
{
    Q_OBJECT
public:
    Q_PROPERTY(QVariant current READ getCurrent NOTIFY currentChanged)
    Q_PROPERTY(bool empty READ isEmpty NOTIFY emptyChanged)

    enum class PostAction{
        Stay,
        Go
    };
    Q_ENUM(PostAction)

public:
    explicit NavigationHistory(QObject *parent = nullptr);

    QVariant getCurrent();
    bool isEmpty();

signals:
    void currentChanged(QVariant current);
    void emptyChanged(bool empty);

public slots:
    //push a new page
    void push( QVariantMap, PostAction = PostAction::Stay );
    //pop the last page
    void pop( PostAction = PostAction::Stay );

private:
    QVariantList m_history;
};

#endif // NAVIGATION_HISTORY_HPP
