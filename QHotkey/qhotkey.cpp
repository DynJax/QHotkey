#include "qhotkey.h"
#include "qhotkey_p.h"
#include <QCoreApplication>
#include <QAbstractEventDispatcher>
#include <QMetaMethod>
#include <QDebug>

#define LOCKER QMutexLocker locker(&this->mutex)

QHotkey::QHotkey(QObject *parent) :
	QObject(parent),
	key(Qt::Key_unknown),
	mods(Qt::NoModifier),
	nativeShortcut(),
	registered(false)
{}

QHotkey::QHotkey(const QKeySequence &sequence, bool autoRegister, QObject *parent) :
	QHotkey(parent)
{
	this->setShortcut(sequence, autoRegister);
}

QHotkey::QHotkey(Qt::Key key, Qt::KeyboardModifiers modifiers, bool autoRegister, QObject *parent) :
	QHotkey(parent)
{
	this->setShortcut(key, modifiers, autoRegister);
}

QHotkey::~QHotkey()
{
	if(this->registered)
		QHotkeyPrivate::instance()->removeShortcut(this);
}

bool QHotkey::isKeyCaptured(Qt::Key key, Qt::KeyboardModifiers modifiers)
{
	return QHotkeyPrivate::instance()->hasShortcut(key, modifiers);
}

QKeySequence QHotkey::shortcut() const
{
	if(this->key == Qt::Key_unknown)
		return QKeySequence();
	else
		return QKeySequence(this->key | this->mods);
}

Qt::Key QHotkey::keyCode() const
{
	return this->key;
}

Qt::KeyboardModifiers QHotkey::modifiers() const
{
	return this->mods;
}

bool QHotkey::isRegistered() const
{
	return this->registered;
}

bool QHotkey::setShortcut(const QKeySequence &shortcut, bool autoRegister)
{
	if(shortcut.isEmpty()) {
		return this->resetShortcut();
	} else if(shortcut.count() > 1) {
		qWarning("QHotkey: Keysequences with multiple shortcuts are not allowed! "
				 "Only the first shortcut will be used!");
	}

	return this->setShortcut(Qt::Key(shortcut[0] & ~Qt::KeyboardModifierMask),
							 Qt::KeyboardModifiers(shortcut[0] & Qt::KeyboardModifierMask),
							 autoRegister);
}

bool QHotkey::setShortcut(Qt::Key key, Qt::KeyboardModifiers modifiers, bool autoRegister)
{
	if(this->registered) {
		if(autoRegister) {
			if(!QHotkeyPrivate::instance()->removeShortcut(this))
				return false;
		} else {
			qWarning("QHotkey: Can't change the shortcut while the hotkey is registered");
			return false;
		}
	}

	this->key = key;
	this->mods = modifiers;
	this->nativeShortcut = QHotkeyPrivate::instance()->nativeShortcut(key, modifiers);
	if(QHotkeyPrivate::testValid(this->nativeShortcut)) {
		if(autoRegister)
			return QHotkeyPrivate::instance()->addShortcut(this);
		else
			return true;
	} else {
        qWarning() << "QHotkey: Unable to map shortcut to native keys. Key:" << key << "Modifiers:" << modifiers;
		this->key = Qt::Key_unknown;
		this->mods = Qt::NoModifier;
		this->nativeShortcut = NativeShortcut();
		return false;
	}
}

bool QHotkey::resetShortcut()
{
	if(this->registered &&
	   !QHotkeyPrivate::instance()->removeShortcut(this)) {
		qWarning("QHotkey: Failed to unregister shortcut");
		return false;
	}

	this->key = Qt::Key_unknown;
	this->mods = Qt::NoModifier;
	this->nativeShortcut = NativeShortcut();
	return true;
}

bool QHotkey::setRegistered(bool registered)
{
	if(this->registered && !registered)
		return QHotkeyPrivate::instance()->removeShortcut(this);
	else if(!this->registered && registered) {
		if(!QHotkeyPrivate::testValid(this->nativeShortcut))
			return false;
		else
			return QHotkeyPrivate::instance()->addShortcut(this);
	} else
		return true;
}



// ---------- QHotkeyPrivate implementation ----------

QHotkeyPrivate::QHotkeyPrivate() :
	mutex(QMutex::NonRecursive),
	shortcuts()
{
	Q_ASSERT_X(qApp, Q_FUNC_INFO, "QHotkey requires QCoreApplication to be instantiated");
	qApp->eventDispatcher()->installNativeEventFilter(this);
}

QHotkeyPrivate::~QHotkeyPrivate()
{
	if(!this->shortcuts.isEmpty())
		qWarning("QHotkey: QHotkeyPrivate destroyed with registered shortcuts!");
	if(qApp && qApp->eventDispatcher())
		qApp->eventDispatcher()->removeNativeEventFilter(this);
}

bool QHotkeyPrivate::hasShortcut(Qt::Key keycode, Qt::KeyboardModifiers modifiers)
{
	return this->shortcuts.contains(this->nativeShortcut(keycode, modifiers));
}

bool QHotkeyPrivate::addShortcut(QHotkey *hotkey)
{
	if(hotkey->registered)
		return false;
	QHotkey::NativeShortcut shortcut = hotkey->nativeShortcut;

	LOCKER;
	if(!this->shortcuts.contains(shortcut)) {
		if(!this->registerShortcut(shortcut))
			return false;
	}

	this->shortcuts.insert(shortcut, hotkey);
	hotkey->registered = true;
	emit hotkey->registeredChanged(true);
	return true;
}

bool QHotkeyPrivate::removeShortcut(QHotkey *hotkey)
{
	if(!hotkey->registered)
		return false;
	QHotkey::NativeShortcut shortcut = hotkey->nativeShortcut;

	LOCKER;
	if(this->shortcuts.remove(shortcut, hotkey) == 0)
		return false;
	hotkey->registered = false;
	emit hotkey->registeredChanged(true);
	if(this->shortcuts.count(shortcut) == 0)
		return this->unregisterShortcut(shortcut);
	else
		return true;
}

void QHotkeyPrivate::activateShortcut(QHotkey::NativeShortcut shortcut)
{
	LOCKER;

	QMetaMethod signal = QMetaMethod::fromSignal(&QHotkey::activated);
	for(QHotkey *hkey : this->shortcuts.values(shortcut))
		signal.invoke(hkey, Qt::QueuedConnection);
}
