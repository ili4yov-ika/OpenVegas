#pragma once

#include "ui/MediaGeneratorDialog.h"

class QCheckBox;
class QComboBox;
class QFontComboBox;
class QLineEdit;
class QSlider;
class QStackedWidget;
class QTableWidget;
class QToolButton;

namespace openvegas {

class CreditPlacement;

/**
 * VEGAS Credit Roll's own page (sftrans1.dll dialogs 228–236): a toolbar and the credits
 * as a list of lines on the left — each a header, a single item or a two-column item,
 * marked by its icon — and two tabs on the right. Properties holds the effect (Scrolling
 * Credits or Timed Sequence), the text area with a picture of it and its Left, Right and
 * Top, and the effect's own settings (scroll direction; or In, Out and Display). Styles
 * holds each line kind's font, size, colour, tracking and spacing, what joins a two-column
 * line's sides, and the background colour.
 *
 * The parameters are the decoded block's (vegLegacyGeneratorParams): Text keeps VEGAS's
 * own text-file form — a leading space for a header, a tab between two columns.
 */
class CreditRollPage : public GeneratorPage {
    Q_OBJECT
public:
    explicit CreditRollPage(QWidget *parent = nullptr);
    void setParams(const QVariantMap &params) override;

private:
    void refresh();
    void edited();
    void tableEdited();
    void insertRow();
    void deleteRows();
    void importText();
    void exportText();
    QVariantMap style(int index) const;
    void setStyle(int index, const QString &key, const QVariant &value);
    int currentStyleIndex() const;

    QVariantMap m_params;
    bool m_block = false;

    QTableWidget *m_table = nullptr;
    QComboBox *m_effect = nullptr;
    CreditPlacement *m_placement = nullptr;
    QLineEdit *m_left = nullptr;
    QLineEdit *m_right = nullptr;
    QLineEdit *m_top = nullptr;
    QStackedWidget *m_effectParams = nullptr;
    QComboBox *m_direction = nullptr;
    QComboBox *m_in = nullptr;
    QComboBox *m_out = nullptr;
    QComboBox *m_display = nullptr;

    QComboBox *m_styleName = nullptr;
    QFontComboBox *m_font = nullptr;
    QComboBox *m_size = nullptr;
    QToolButton *m_color = nullptr;
    QCheckBox *m_bold = nullptr;
    QSlider *m_trackingSlider = nullptr;
    QLineEdit *m_tracking = nullptr;
    QSlider *m_spacingSlider = nullptr;
    QLineEdit *m_spacing = nullptr;
    QComboBox *m_connector = nullptr;
    QToolButton *m_background = nullptr;
};

} // namespace openvegas
