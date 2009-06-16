/*
    Generator for the SMOKE sources
    Copyright (C) 2009 Arno Rehn <arno@arnorehn.de>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License along
    with this program; if not, write to the Free Software Foundation, Inc.,
    51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
*/

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QMap>
#include <QSet>
#include <QTextStream>

#include <type.h>

#include "globals.h"

SmokeClassFiles::SmokeClassFiles(SmokeDataFile *data)
    : m_smokeData(data)
{
}

void SmokeClassFiles::write()
{
    write(m_smokeData->includedClasses);
}

void SmokeClassFiles::write(const QList<QString>& keys)
{
    // how many classes go in one file
    int count = keys.count() / Options::parts;
    int count2 = count;
    
    for (int i = 0; i < Options::parts; i++) {
        QSet<QString> includes;
        QString classCode;
        QTextStream classOut(&classCode);
        
        // write the class code to a QString so we can later prepend the #includes
        if (i == Options::parts - 1) count2 = -1;
        foreach (const QString& str, keys.mid(count * i, count2)) {
            const Class* klass = &classes[str];
            includes.insert(klass->fileName());
            writeClass(classOut, klass, str);
        }
        
        // create the file
        QFile file(Options::outputDir.filePath("x_" + QString::number(i + 1) + ".cpp"));
        file.open(QFile::ReadWrite | QFile::Truncate);

        QTextStream fileOut(&file);
        
        // write out the header
        fileOut << "//Auto-generated by " << QCoreApplication::arguments()[0] << ". DO NOT EDIT.\n";
        fileOut << "#include <smoke.h>\n#include <" << Options::module << "_smoke.h>\n";

        // ... and the #includes
        QList<QString> sortedIncludes = includes.toList();
        qSort(sortedIncludes.begin(), sortedIncludes.end());
        foreach (const QString& str, sortedIncludes) {
            fileOut << "#include <" << str << ">\n";
        }

        // now the class code
        fileOut << "\n" << classCode;
        
        file.close();
    }
}

void SmokeClassFiles::generateMethod(QTextStream& out, const QString& className, const QString& smokeClassName, const Method& meth, int index)
{
    out << "    ";
    if (meth.flags() & Method::Static)
        out << "static ";
    out << QString("void x_%1(Smoke::Stack x) {\n").arg(index + 1);
    out << "        // " << meth.toString() << "\n";
    out << "        ";
    if (meth.isConstructor()) {
        out << smokeClassName << "* xret = new " << smokeClassName << "(";
    } else {
        if (meth.type() != Type::Void)
            out << meth.type()->toString() << " xret = ";
        if (!(meth.flags() & Method::Static))
            out << "this->";
        out << className << "::" << meth.name() << "(";
    }
    for (int j = 0; j < meth.parameters().count(); j++) {
        const Parameter& param = meth.parameters()[j];
        if (j > 0) out << ",";
        QString field = Util::stackItemField(param.type());
        QString typeName = param.type()->toString();
        if (field == "s_class" && param.type()->pointerDepth() == 0) {
            // strip the reference symbol.. we can't have pointers to references
            if (param.type()->isRef()) typeName.replace('&', "");
            typeName.append('*');
            out << '*';
        }
        out << "(" << typeName << ")" << "x[" << j + 1 << "]." << field;
    }
    out << ");\n";
    if (meth.type() != Type::Void) {
        out << "        x[0]." << Util::stackItemField(meth.type()) << " = " << Util::assignmentString(meth.type(), "xret") << ";\n";
    } else {
        out << "        (void)x; // noop (for compiler warning)\n";
    }
    out << "    }\n";
    if (meth.isConstructor()) {
        out << "    explicit " << smokeClassName << '(';
        QStringList x_list;
        for (int i = 0; i < meth.parameters().count(); i++) {
            if (i > 0) out << ", ";
            out << meth.parameters()[i].type()->toString() << " x" << QString::number(i + 1);
            x_list << "x" + QString::number(i + 1);
        }
        out << ") : " << meth.getClass()->name() << '(' << x_list.join(", ") << ") {}\n";
    }
}

void SmokeClassFiles::generateVirtualMethod(QTextStream& out, const QString& className, const Method& meth)
{
    QString x_params, x_list;
    QString type = meth.type()->toString();
    out << "    virtual " << type << " " << meth.name() << "(";
    for (int i = 0; i < meth.parameters().count(); i++) {
        if (i > 0) { out << ", "; x_list.append(", "); }
        const Parameter& param = meth.parameters()[i];
        out << param.type()->toString() << " x" << i + 1;
        x_params += QString("        x[%1].%2 = %3;\n")
            .arg(QString::number(i + 1)).arg(Util::stackItemField(param.type()))
            .arg(Util::assignmentString(param.type(), "x" + QString::number(i + 1)));
        x_list += "x" + QString::number(i + 1);
    }
    out << ") {\n";
    out << QString("        Smoke::StackItem x[%1];\n").arg(meth.parameters().count() + 1);
    out << x_params;
    out << QString("        if (this->_binding->callMethod(%1, (void*)this, x)) ").arg(m_smokeData->classIndex[className]);
    if (meth.type() == Type::Void) {
        out << "return;\n";
    } else {
        QString field = Util::stackItemField(meth.type());
        if (meth.type()->pointerDepth() == 0 && field == "s_class") {
            QString tmpType = type;
            if (meth.type()->isRef()) tmpType.replace('&', "");
            tmpType.append('*');
            out << "{\n";
            out << "            " << tmpType << " xptr = (" << tmpType << ")x[0].s_class;\n";
            out << "            " << type << " xret(*xptr);\n";
            out << "            delete xptr;\n";
            out << "            return xret;\n";
            out << "        }\n";
        } else {
            out << QString("return (%1)x[0].%2;\n").arg(type, Util::stackItemField(meth.type()));
        }
    }
    out << "        ";
    if (meth.type() != Type::Void)
        out << "return ";
    out << QString("this->%1::%2(%3);\n").arg(className).arg(meth.name()).arg(x_list);
    out << "    }\n";
}

void SmokeClassFiles::writeClass(QTextStream& out, const Class* klass, const QString& className)
{
    const QString smokeClassName = "x_" + QString(className).replace("::", "__");
    
    out << QString("class %1 : public %2 {\n").arg(smokeClassName).arg(className);
    out << "    SmokeBinding* _binding;\n";
    out << "public:\n";
    out << "    void x_0(Smoke::Stack x) {\n";
    out << "        // set the smoke binding\n";
    out << "        _binding = (SmokeBinding*)x[1].s_class;\n";
    out << "    }\n";
    for(int i = 0; i < klass->methods().count(); i++) {
        const Method& meth = klass->methods()[i];
        if (meth.access() == Access_private)
            continue;
        generateMethod(out, className, smokeClassName, meth, i);
    }
    foreach (const Method* meth, Util::collectVirtualMethods(klass)) {
        generateVirtualMethod(out, className, *meth);
    }
    // destructor
    out << "    ~" << smokeClassName << QString("() { this->_binding->deleted(%1, (void*)this); }\n").arg(m_smokeData->classIndex[className]);
    out << "}\n\n";
}
