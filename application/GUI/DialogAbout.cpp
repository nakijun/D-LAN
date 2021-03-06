/**
  * D-LAN - A decentralized LAN file sharing software.
  * Copyright (C) 2010-2012 Greg Burri <greg.burri@gmail.com>
  *
  * This program is free software: you can redistribute it and/or modify
  * it under the terms of the GNU General Public License as published by
  * the Free Software Foundation, either version 3 of the License, or
  * (at your option) any later version.
  *
  * This program is distributed in the hope that it will be useful,
  * but WITHOUT ANY WARRANTY; without even the implied warranty of
  * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  * GNU General Public License for more details.
  *
  * You should have received a copy of the GNU General Public License
  * along with this program.  If not, see <http://www.gnu.org/licenses/>.
  */
  
#include <DialogAbout.h>
#include <ui_DialogAbout.h>
using namespace GUI;

#include <QPainter>
#include <QDateTime>
#include <QLocale>

#include <Common/Version.h>
#include <Common/Global.h>
#include <Common/Settings.h>

DialogAbout::DialogAbout(QWidget *parent) :
   QDialog(parent), ui(new Ui::DialogAbout)
{
   this->ui->setupUi(this);

   this->setWindowFlags(this->windowFlags() & (~Qt::WindowContextHelpButtonHint));

   QDateTime buildTime = QDateTime::fromString(BUILD_TIME, "yyyy-MM-dd_hh-mm");

   QLocale locale = SETTINGS.get<QLocale>("language");

   this->ui->lblTitle->setText(QString("%1 %2 %3").arg(this->ui->lblTitle->text()).arg(VERSION).arg(VERSION_TAG));
   this->ui->lblBuiltOn->setText(QString("%1 %2").arg(this->ui->lblBuiltOn->text()).arg(locale.toString(buildTime)));
   this->ui->lblFromRevision->setText(QString("<html><head/><body><p>%1 <a href=\"https://github.com/Ummon/D-LAN/commit/%2\"><span style=\"color: #fd2435\">%2</span></a></p></body></html>").arg(this->ui->lblFromRevision->text()).arg(GIT_VERSION));
   this->ui->lblCopyright->setText(this->ui->lblCopyright->text().arg(buildTime.date().year()));
   const QString& compilerName = Common::Global::getCompilerName();
   const QString& compilerVersion = Common::Global::getCompilerVersion();

   if (compilerName.isEmpty())
      this->ui->lblCompiler->setText(QString("%1 Qt %4").arg(this->ui->lblCompiler->text()).arg(QT_VERSION_STR));
   else
      this->ui->lblCompiler->setText(QString("%1 %2 %3 - Qt %4").arg(this->ui->lblCompiler->text()).arg(compilerName).arg(compilerVersion).arg(QT_VERSION_STR));

#ifdef DEBUG
   this->ui->lblTitle->setText(this->ui->lblTitle->text() + " (DEBUG)");
#endif
}

DialogAbout::~DialogAbout()
{
   delete this->ui;
}

/**
  * Draw a nice gradient sky as background.
  */
void DialogAbout::paintEvent(QPaintEvent* event)
{
   QPainter p(this);
   QRadialGradient gradient(QPointF(0, 0), 2, QPointF(0, 0));
   gradient.setCoordinateMode(QGradient::StretchToDeviceMode);
   gradient.setColorAt(0, QColor(24, 36, 48));
   gradient.setColorAt(1, QColor(102, 150, 201));
   QBrush brush(gradient);
   p.fillRect(QRect(0, 0, width(), height()), brush);
}


void DialogAbout::changeEvent(QEvent* event)
{
   if (event->type() == QEvent::LanguageChange)
      this->ui->retranslateUi(this);

   QDialog::changeEvent(event);
}
