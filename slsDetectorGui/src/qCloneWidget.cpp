#include "qCloneWidget.h"
#include "qDefs.h"
#include "SlsQt1DPlot.h"
#include "SlsQt2DPlotLayout.h"

#include "qwt_symbol.h"
#include <QWidget>
#include <QCloseEvent>
#include <QGridLayout>
#include <QGroupBox>
#include <QLabel>
#include <QAction>
#include <QCheckBox>
#include <QComboBox>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QMenu>
#include <QMenuBar>
#include <QPushButton>
#include <QSpacerItem>
#include <QFileDialog>
#include <QImage>
#include <QPainter>

qCloneWidget::qCloneWidget(QWidget *parent, int id, QString title, QString xTitle, QString yTitle, QString zTitle,
                           int numDim, QString fPath, QString fName, int fIndex, bool displayStats, QString min, QString max, QString sum) : 
                           QMainWindow(parent), id(id), filePath(fPath), fileName(fName), fileIndex(fIndex), cloneplot1D(nullptr), cloneplot2D(nullptr),
                           marker(nullptr), nomarker(nullptr), mainLayout(nullptr), cloneBox(nullptr), lblHistTitle(nullptr) {
    // Window title
    char winTitle[300], currTime[50];
    strcpy(currTime, GetCurrentTimeStamp());
    sprintf(winTitle, "Snapshot:%d  -  %s", id, currTime);
    setWindowTitle(QString(winTitle));

    marker = new QwtSymbol();
    nomarker = new QwtSymbol();
    marker->setStyle(QwtSymbol::Cross);
    marker->setSize(5, 5);

    // Set up widget
    SetupWidgetWindow(title, xTitle, yTitle, zTitle, numDim);
    DisplayStats(displayStats, min, max, sum);
}

qCloneWidget::~qCloneWidget() {
    if (cloneplot1D)
        delete cloneplot1D;
    if (cloneplot2D)
        delete cloneplot2D;
    cloneplot1D_hists.clear();    
    if (marker)
        delete marker;
    if (nomarker)
        delete nomarker;  
    if (mainLayout)
        delete mainLayout; 
    if (cloneBox)
        delete cloneBox;        
    if (lblHistTitle)
        delete lblHistTitle;
}

SlsQt1DPlot* qCloneWidget::Get1dPlot() {
	return cloneplot1D;
}

void qCloneWidget::SetupWidgetWindow(QString title, QString xTitle, QString yTitle, QString zTitle, int numDim) {

	QMenuBar* menubar = new QMenuBar(this);
	QAction* actionSave = new QAction("&Save", this);
    menubar->addAction(actionSave);
    setMenuBar(menubar);

    //Main Window Layout
    QWidget *centralWidget = new QWidget(this);
    mainLayout = new QGridLayout(centralWidget);
    centralWidget->setLayout(mainLayout);

    //plot group box
    cloneBox = new QGroupBox(this);
    QGridLayout* gridClone = new QGridLayout(cloneBox);
    cloneBox->setLayout(gridClone);
    cloneBox->setContentsMargins(0, 0, 0, 0);
    cloneBox->setAlignment(Qt::AlignHCenter);
    cloneBox->setFont(QFont("Sans Serif", 11, QFont::Normal));
    cloneBox->setTitle(title);
    // According to dimensions, create appropriate 1D or 2Dplot
    if (numDim == 1) {
        cloneplot1D = new SlsQt1DPlot(cloneBox);

        cloneplot1D->setFont(QFont("Sans Serif", 9, QFont::Normal));
        cloneplot1D->SetXTitle(xTitle.toAscii().constData());
        cloneplot1D->SetYTitle(yTitle.toAscii().constData());

        cloneBox->setFlat(false);
        cloneBox->setContentsMargins(0, 30, 0, 0);
        gridClone->addWidget(cloneplot1D, 0, 0);

        lblHistTitle = new QLabel("");
        mainLayout->addWidget(lblHistTitle, 0, 0);

    } else {
        cloneplot2D = new SlsQt2DPlotLayout(cloneBox);
        cloneplot2D->setFont(QFont("Sans Serif", 9, QFont::Normal));
        cloneplot2D->SetXTitle(xTitle);
        cloneplot2D->SetYTitle(yTitle);
        cloneplot2D->SetZTitle(zTitle);
        cloneplot2D->setAlignment(Qt::AlignLeft);

        cloneBox->setFlat(true);
        cloneBox->setContentsMargins(0, 20, 0, 0);
        gridClone->addWidget(cloneplot2D, 0, 0);
    }

    // main window widgets
    mainLayout->addWidget(cloneBox, 1, 0);
    setCentralWidget(centralWidget);

    // Save
    connect(actionSave, SIGNAL(triggered()), this, SLOT(SavePlot()));

    setMinimumHeight(300);
    resize(500, 350);
}

void qCloneWidget::SetCloneHists(unsigned int nHists, int histNBins, double *histXAxis, std::vector<double*> histYAxis, std::vector<std::string> histTitle, bool lines, bool markers) {
    //for each plot,  create hists
    for (unsigned int hist_num = 0; hist_num < nHists; ++hist_num) {
        SlsQtH1D *k;
        if (hist_num + 1 > cloneplot1D_hists.size()) {
            cloneplot1D_hists.append(k = new SlsQtH1D("1d plot", histNBins, histXAxis, histYAxis[hist_num]));
            k->SetLineColor(0);
        } else {
            k = cloneplot1D_hists.at(hist_num);
            k->SetData(histNBins, histXAxis, histYAxis[hist_num]);
        }

        //style of plot
        if (lines)
            k->setStyle(QwtPlotCurve::Lines);
        else
            k->setStyle(QwtPlotCurve::Dots);
#if QWT_VERSION < 0x060000
        if (markers)
            k->setSymbol(*marker);
        else
            k->setSymbol(*nomarker);
#else
        if (markers)
            k->setSymbol(marker);
        else
            k->setSymbol(nomarker);
#endif

        //set title and attach plot
        lblHistTitle->setText(QString(histTitle[0].c_str()));

        k->Attach(cloneplot1D);
    }
}

void qCloneWidget::SetCloneHists2D(int nbinsx, double xmin, double xmax, int nbinsy, double ymin, double ymax, double *d) {
    cloneplot2D->GetPlot()->SetData(nbinsx, xmin, xmax, nbinsy, ymin, ymax, d);
    cloneplot2D->KeepZRangeIfSet();
}

void qCloneWidget::SetRange(bool IsXYRange[], double XYRangeValues[]) {
    double XYCloneRangeValues[4];

    if (!IsXYRange[qDefs::XMINIMUM]) {
        if (cloneplot1D)
            XYCloneRangeValues[qDefs::XMINIMUM] = cloneplot1D->GetXMinimum();
        else
            XYCloneRangeValues[qDefs::XMINIMUM] = cloneplot2D->GetPlot()->GetXMinimum();
    } else
        XYCloneRangeValues[qDefs::XMINIMUM] = XYRangeValues[qDefs::XMINIMUM];

    if (!IsXYRange[qDefs::XMAXIMUM]) {
        if (cloneplot1D)
            XYCloneRangeValues[qDefs::XMAXIMUM] = cloneplot1D->GetXMaximum();
        else
            XYCloneRangeValues[qDefs::XMAXIMUM] = cloneplot2D->GetPlot()->GetXMaximum();
    } else
        XYCloneRangeValues[qDefs::XMAXIMUM] = XYRangeValues[qDefs::XMAXIMUM];

    if (!IsXYRange[qDefs::YMINIMUM]) {
        if (cloneplot1D)
            XYCloneRangeValues[qDefs::YMINIMUM] = cloneplot1D->GetYMinimum();
        else
            XYCloneRangeValues[qDefs::YMINIMUM] = cloneplot2D->GetPlot()->GetYMinimum();
    } else
        XYCloneRangeValues[qDefs::YMINIMUM] = XYRangeValues[qDefs::YMINIMUM];

    if (!IsXYRange[qDefs::YMAXIMUM]) {
        if (cloneplot1D)
            XYCloneRangeValues[qDefs::YMAXIMUM] = cloneplot1D->GetYMaximum();
        else
            XYCloneRangeValues[qDefs::YMAXIMUM] = cloneplot2D->GetPlot()->GetYMaximum();
    } else
        XYCloneRangeValues[qDefs::YMAXIMUM] = XYRangeValues[qDefs::YMAXIMUM];

    if (cloneplot1D) {
        cloneplot1D->SetXMinMax(XYCloneRangeValues[qDefs::XMINIMUM], XYCloneRangeValues[qDefs::XMAXIMUM]);
        cloneplot1D->SetYMinMax(XYCloneRangeValues[qDefs::YMINIMUM], XYCloneRangeValues[qDefs::YMAXIMUM]);
    } else {
        cloneplot2D->GetPlot()->SetXMinMax(XYRangeValues[qDefs::XMINIMUM], XYRangeValues[qDefs::XMAXIMUM]);
        cloneplot2D->GetPlot()->SetYMinMax(XYRangeValues[qDefs::YMINIMUM], XYRangeValues[qDefs::YMAXIMUM]);
        cloneplot2D->GetPlot()->Update();
    }
}

void qCloneWidget::SavePlot() {
    char cID[10];
    sprintf(cID, "%d", id);
    //title
    QString fName = filePath + Qstring('/') + fileName + Qstring('_') + imageIndex +  Qstring('_') + QString(NowTime().c_str()) + QString(".png");
    FILE_LOG(logDEBUG) << "fname:" << fName.toAscii().constData();
    //save
    QImage img(cloneBox->size().width(), cloneBox->size().height(), QImage::Format_RGB32);
    QPainter painter(&img);
    cloneBox->render(&painter);

    fName = QFileDialog::getSaveFileName(this, tr("Save Snapshot "), fName, tr("PNG Files (*.png);;XPM Files(*.xpm);;JPEG Files(*.jpg)"), 0, QFileDialog::ShowDirsOnly);
    if (!fName.isEmpty()) {
        if ((img.save(fName))) {
            qDefs::Message(qDefs::INFORMATION, "The SnapShot has been successfully saved", "qCloneWidget::SavePlot");
            FILE_LOG(logINFO) << "The SnapShot has been successfully saved";
        } else {
            qDefs::Message(qDefs::WARNING, "Attempt to save snapshot failed.\n"
                                           "Formats: .png, .jpg, .xpm.",
                           "qCloneWidget::SavePlot");
            FILE_LOG(logWARNING) << "Attempt to save snapshot failed";
        }
    }
}

int qCloneWidget::SavePlotAutomatic() {
    char cID[10];
    sprintf(cID, "%d", id);
    //title
    QString fName = filePath + Qstring('/') + fileName + Qstring('_') + imageIndex +  Qstring('_') + QString(NowTime().c_str()) + QString(".png");
    FILE_LOG(logDEBUG) << "fname:" << fName.toAscii().constData();
    //save
    QImage img(cloneBox->size().width(), cloneBox->size().height(), QImage::Format_RGB32);
    QPainter painter(&img);
    cloneBox->render(&painter);
    if (img.save(fName))
        return 0;
    else
        return -1;
}

void qCloneWidget::closeEvent(QCloseEvent *event) {
    emit CloneClosedSignal(id);
    event->accept();
}

char *qCloneWidget::GetCurrentTimeStamp() {
    char output[30];
    char *result;

    //using sys cmds to get output or str
    FILE *sysFile = popen("date", "r");
    fgets(output, sizeof(output), sysFile);
    pclose(sysFile);

    result = output + 0;
    return result;
}

void qCloneWidget::DisplayStats(bool enable, QString min, QString max, QString sum) {
    if (enable) {
        QWidget *widgetStatistics = new QWidget(this);
        widgetStatistics->setFixedHeight(15);
        QHBoxLayout *hl1 = new QHBoxLayout;
        hl1->setSpacing(0);
        hl1->setContentsMargins(0, 0, 0, 0);
        QLabel *lblMin = new QLabel("Min:  ");
        lblMin->setFixedWidth(40);
        lblMin->setAlignment(Qt::AlignRight);
        QLabel *lblMax = new QLabel("Max:  ");
        lblMax->setFixedWidth(40);
        lblMax->setAlignment(Qt::AlignRight);
        QLabel *lblSum = new QLabel("Sum:  ");
        lblSum->setFixedWidth(40);
        lblSum->setAlignment(Qt::AlignRight);
        QLabel *lblMinDisp = new QLabel(min);
        lblMinDisp->setAlignment(Qt::AlignLeft);
        QLabel *lblMaxDisp = new QLabel(max);
        lblMaxDisp->setAlignment(Qt::AlignLeft);
        QLabel *lblSumDisp = new QLabel(sum);
        lblSumDisp->setAlignment(Qt::AlignLeft);
        hl1->addItem(new QSpacerItem(20, 20, QSizePolicy::Fixed, QSizePolicy::Fixed));
        hl1->addWidget(lblMin);
        hl1->addWidget(lblMinDisp);
        hl1->addItem(new QSpacerItem(20, 20, QSizePolicy::Expanding, QSizePolicy::Fixed));
        hl1->addWidget(lblMax);
        hl1->addWidget(lblMaxDisp);
        hl1->addItem(new QSpacerItem(20, 20, QSizePolicy::Expanding, QSizePolicy::Fixed));
        hl1->addWidget(lblSum);
        hl1->addWidget(lblSumDisp);
        hl1->addItem(new QSpacerItem(20, 20, QSizePolicy::Fixed, QSizePolicy::Fixed));
        widgetStatistics->setLayout(hl1);
        mainLayout->addWidget(widgetStatistics, 2, 0);
        widgetStatistics->show();
    }
}

