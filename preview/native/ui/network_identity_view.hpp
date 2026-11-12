// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "network_identity.hpp"
#include <QWidget>
class QLabel;
namespace biglybtc::ui {
class NetworkIdentityView final:public QWidget {
public:
 explicit NetworkIdentityView(QWidget* parent=nullptr);
 void setSnapshot(std::shared_ptr<const services::network_identity::Snapshot>);
private:
 std::array<std::array<QLabel*,5>,2> labels_{};
};
}
