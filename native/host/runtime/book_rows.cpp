#include "book_rows.hpp"
#include "navigation.hpp"
#include "combat_vision.hpp"
#include <QSet>
#include <opencv2/imgproc.hpp>
namespace okww::runtime {
QVector<BookRow> readBookRows(TaskContext &c) {
    const auto area=gameBox(c.frame,.82,.205,.97,.88);
    auto buttons=c.templates.find(c.frame,"boss_proceed",{.threshold=.76,.search=area,.limit=0});
    if(c.streamedImage()) {
        buttons+=c.templates.find(c.frame,"boss_proceed",{.threshold=.72,.search=area,.grayscale=true,.limit=0,.blurSigma=.55});
    }
    buttons.erase(std::remove_if(buttons.begin(),buttons.end(),[&](const auto &button){
        const double y=button.center().y()/c.frame.rows;
        const auto band=gameBox(c.frame,.875,y-.006,.935,y+.006);
        cv::Mat gray,dark;
        cv::cvtColor(c.frame(cv::Rect(band.x(),band.y(),band.width(),band.height())),gray,cv::COLOR_BGR2GRAY);
        cv::threshold(gray,dark,80,255,cv::THRESH_BINARY_INV);
        // A matching right edge alone also occurs on the region group headers.
        return cv::countNonZero(dark)<dark.total()*.20;
    }),buttons.end());
    const auto text=c.text(.39,.18,.97,.88,.65);
    for(const auto &line:text) {
        if(line.center().x()<c.frame.cols*.84 || !QRegularExpression(
            "^(?:前往|传送|傳送|直接挑[战戰]|Proceed|Go\\s*to|Teleport|(?:Direct\\s*)?Challenge|Challenge\\s*Directly)$",
            QRegularExpression::CaseInsensitiveOption).match(line.text.trimmed()).hasMatch())continue;
        buttons.append({"boss_proceed",line.box,line.confidence});
    }
    std::sort(buttons.begin(),buttons.end(),[](const auto &a,const auto &b){return a.confidence>b.confidence;});
    QVector<Match> unique;
    for(const auto &button:buttons) {
        if(std::none_of(unique.begin(),unique.end(),[&](const auto &other){
            return std::abs(other.center().y()-button.center().y())<c.frame.rows*.018;
        }))unique.append(button);
    }
    std::sort(unique.begin(),unique.end(),[](const auto &a,const auto &b){return a.center().y()<b.center().y();});
    QVector<BookRow> rows;
    for(const auto &button:unique) {
        const double y=button.center().y()/c.frame.rows;
        const auto region=gameBox(c.frame,.40,std::max(.18,y-.10),.87,std::min(.88,y+.025));
        cv::Mat gray,small;
        cv::cvtColor(c.frame(cv::Rect(region.x(),region.y(),region.width(),region.height())),gray,cv::COLOR_BGR2GRAY);
        cv::resize(gray,small,{192,48},0,0,cv::INTER_AREA);
        cv::GaussianBlur(small,small,{3,3},.55);
        QStringList names;
        for(const auto &line:text) {
            if(region.contains(line.center().toPoint()) && line.center().y()<button.center().y())
                names.append(normalizeNavigationText(line.text));
        }
        rows.append({button,small,names.join('|')});
    }
    return rows;
}
double bookRowVisualSimilarity(const BookRow &a,const BookRow &b) {
    if(a.identity.empty() || b.identity.empty())return 0.;
    cv::Scalar mean, deviation;
    cv::meanStdDev(a.identity,mean,deviation);
    if(deviation[0]<3.)return 0.;
    cv::Mat search,scores;
    cv::copyMakeBorder(b.identity,search,2,2,3,3,cv::BORDER_REPLICATE);
    cv::matchTemplate(search,a.identity,scores,cv::TM_CCOEFF_NORMED);
    double score=0;cv::minMaxLoc(scores,nullptr,&score);
    return std::isfinite(score) ? score : 0.;
}
double bookRowSimilarity(const BookRow &a,const BookRow &b,bool stationary) {
    if(!a.title.isEmpty() && a.title.size()>=3 && a.title==b.title)return 1.;
    const double visual=bookRowVisualSimilarity(a,b);
    // Only a no-input observation may disregard conflicting OCR. After scrolling,
    // adjacent entries can share almost the same artwork and reward description.
    if(stationary && visual>=.90)return visual;
    if(!a.title.isEmpty() && !b.title.isEmpty()) {
        const auto tokens=[](const QString &title) {
            QSet<QString> result;
            for(auto part:title.split('|',Qt::SkipEmptyParts)) {
                part=normalizeNavigationText(part);
                part.remove(QRegularExpression("[^\\p{L}\\p{N}]"));
                if(!part.isEmpty())result.insert(part);
            }
            return result;
        };
        const auto left=tokens(a.title),right=tokens(b.title);
        bool anchor=false;
        for(const auto &part:left)if(part.size()>=3 && right.contains(part))anchor=true;
        // Missing OCR lines or changed reading order are not conflicting names.
        // Two different nonempty names still veto a visual lookalike.
        if(!anchor || !(left.contains(right) || right.contains(left)))return 0.;
        return visual>=.86 ? visual : 0.;
    }
    return visual;
}
bool bookListAtTop(TaskContext &c,int total) {
    const auto bars=colorRectangles(c.frame,{{190,190,190},{255,255,255}},
        std::max(1.,c.frame.cols*3./2560),std::max(3.,c.frame.rows*8./1440),-1,
        gameBox(c.frame,.968,.188,.980,.884),.75);
    if(bars.empty())return total<=4;
    return std::any_of(bars.begin(),bars.end(),[&](const auto &bar){
        return bar.box.y()<=c.frame.rows*.211;
    });
}
} // namespace okww::runtime
