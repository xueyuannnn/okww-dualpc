#pragma once
#include "task.hpp"
namespace okww::runtime {
struct BookRow {
    Match button;
    cv::Mat identity;
    QString title;
};
QVector<BookRow> readBookRows(TaskContext &context);
double bookRowSimilarity(const BookRow &left, const BookRow &right);
bool bookListAtTop(TaskContext &context, int total);
} // namespace okww::runtime
