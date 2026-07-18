#pragma once

#include <QComboBox>

namespace quizapp::ui {

class ChoiceComboBox final : public QComboBox
{
public:
    explicit ChoiceComboBox(QWidget *parent = nullptr);
};

} // namespace quizapp::ui
