// main.cpp
#include <QApplication>
#include <QWidget>
#include <QPushButton>
#include <QLineEdit>
#include <QLabel>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFileDialog>
#include <QFile>
#include <QCryptographicHash>
#include <QMessageBox>
#include <QComboBox>
#include <QTextStream>
#include <QDragEnterEvent>
#include <QMimeData>

static const char* WORDS[] = {
    "TACO","MOON","CAT","BIRD","LASER","SPICE","WIZARD","GHOST","DRAGON","FROG",
    "PIZZA","NACHO","APPLE","ROBOT","COOKIE","ZEBRA","POTATO","BANANA"
};
static const int WORD_COUNT = sizeof(WORDS)/sizeof(WORDS[0]);

QString makePlate(const QByteArray& hash)
{
    static const char alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
    QString plate;
    for(int i=0;i<6;i++){
        uchar v = hash[i];
        plate += alphabet[v % (sizeof(alphabet)-1)];
        if(i==2) plate += "-";
    }
    return plate;
}

QString makeWordHash(const QByteArray& hash)
{
    int a = hash[0] % WORD_COUNT;
    int b = hash[1] % WORD_COUNT;
    int num = (uchar)hash[2];
    return QString("%1-%2-%3").arg(WORDS[a]).arg(WORDS[b]).arg(num);
}

QByteArray hashFile(const QString& path, QCryptographicHash::Algorithm algo)
{
    QFile f(path);
    if(!f.open(QFile::ReadOnly)) return QByteArray();
    QCryptographicHash hash(algo);
    while(!f.atEnd()) hash.addData(f.read(1<<20));
    return hash.result();
}

// Simple plain-text embedding
bool embedPlainText(const QString& src, const QString& hash, QString& outpath)
{
    QFile in(src);
    if(!in.open(QFile::ReadOnly)) return false;
    QByteArray data = in.readAll();
    in.close();

    outpath = src + ".hashcopy";
    QFile out(outpath);
    if(!out.open(QFile::WriteOnly)) return false;
    out.write(data);
    out.write("\n<!-- FUNNY-HASH: ");
    out.write(hash.toUtf8());
    out.write(" -->\n");
    out.close();
    return true;
}

// PNG tEXt chunk embedding (simplified)
bool embedPngChunk(const QString& src, const QString& hash, QString& outpath)
{
    QFile in(src);
    if(!in.open(QFile::ReadOnly)) return false;
    QByteArray data = in.readAll();
    in.close();

    // check PNG header
    if(!data.startsWith("\x89PNG\r\n\x1a\n")) return false;

    outpath = src + ".hashpng";
    QFile out(outpath);
    if(!out.open(QFile::WriteOnly)) return false;

    // copy original header
    out.write(data.left(8));

    // iterate chunks until IEND
    int pos = 8;
    while(pos+8 <= data.size()){
        quint32 length = qFromBigEndian<quint32>((uchar*)data.mid(pos,4).data());
        QByteArray type = data.mid(pos+4,4);
        QByteArray chunkData = data.mid(pos+8,length);
        QByteArray crc = data.mid(pos+8+length,4);

        out.write(data.mid(pos, 8+length+4));
        pos += 8+length+4;

        if(type=="IEND"){
            // insert tEXt before IEND
            QByteArray txt;
            txt.append(char(0)); // dummy keyword
            txt.append("FunnyHash=");
            txt.append(hash.toUtf8());
            quint32 len = txt.size();
            QByteArray lenBytes(4,0);
            lenBytes[0] = (len>>24)&0xFF;
            lenBytes[1] = (len>>16)&0xFF;
            lenBytes[2] = (len>>8)&0xFF;
            lenBytes[3] = len&0xFF;
            out.write(lenBytes);
            out.write("tEXt");
            out.write(txt);
            out.write("\0\0\0\0"); // dummy CRC
        }
    }
    out.close();
    return true;
}

class FunnyHashWidget : public QWidget{
    Q_OBJECT
public:
    FunnyHashWidget(){
        setAcceptDrops(true);
        QVBoxLayout* layout = new QVBoxLayout(this);

        fileEdit = new QLineEdit;
        fileEdit->setReadOnly(true);
        layout->addWidget(fileEdit);

        QLabel* algoLabel = new QLabel("Select Hash Algorithm:");
        layout->addWidget(algoLabel);
        algoCombo = new QComboBox;
        algoCombo->addItem("MD5", QCryptographicHash::Md5);
        algoCombo->addItem("SHA-1", QCryptographicHash::Sha1);
        algoCombo->addItem("SHA-256", QCryptographicHash::Sha256);
        layout->addWidget(algoCombo);

        QPushButton* pickBtn = new QPushButton("Load File");
        layout->addWidget(pickBtn);

        hashLabel = new QLabel("Funny Hash will appear here.");
        layout->addWidget(hashLabel);

        QPushButton* genBtn = new QPushButton("Generate Funny Hash");
        layout->addWidget(genBtn);

        QPushButton* embedBtn = new QPushButton("Embed Hash");
        layout->addWidget(embedBtn);

        QPushButton* verifyBtn = new QPushButton("Verify Embedded Hash");
        layout->addWidget(verifyBtn);

        connect(pickBtn,&QPushButton::clicked,[&](){
            QString f = QFileDialog::getOpenFileName(this,"Choose file");
            if(!f.isEmpty()) { selectedFile=f; fileEdit->setText(f); }
        });

        connect(genBtn,&QPushButton::clicked,[&](){
            if(selectedFile.isEmpty()){ QMessageBox::warning(this,"Error","Load a file first."); return; }
            QCryptographicHash::Algorithm algo = (QCryptographicHash::Algorithm)algoCombo->currentData().toInt();
            QByteArray h = hashFile(selectedFile, algo);
            currentFunnyHash = makePlate(h)+" | "+makeWordHash(h);
            hashLabel->setText("Funny Hash:\n"+currentFunnyHash);
        });

        connect(embedBtn,&QPushButton::clicked,[&](){
            if(selectedFile.isEmpty()||currentFunnyHash.isEmpty()){ QMessageBox::warning(this,"Error","Load file and generate hash first."); return; }
            QString out;
            if(selectedFile.endsWith(".png",Qt::CaseInsensitive) && embedPngChunk(selectedFile,currentFunnyHash,out)){
                QMessageBox::information(this,"Done","Embedded PNG chunk created:\n"+out);
            } else if(embedPlainText(selectedFile,currentFunnyHash,out)){
                QMessageBox::information(this,"Done","Embedded plain-text file created:\n"+out);
            } else QMessageBox::warning(this,"Error","Failed to embed hash.");
        });

        connect(verifyBtn,&QPushButton::clicked,[&](){
            if(selectedFile.isEmpty()){ QMessageBox::warning(this,"Error","Load a file first."); return; }
            QFile f(selectedFile); if(!f.open(QFile::ReadOnly)) return;
            QByteArray data=f.readAll(); f.close();
            if(data.contains(currentFunnyHash.toUtf8())) QMessageBox::information(this,"Verify","Hash FOUND inside file!");
            else QMessageBox::information(this,"Verify","Hash NOT found inside file.");
        });
    }

protected:
    void dragEnterEvent(QDragEnterEvent *event) override {
        if(event->mimeData()->hasUrls()) event->acceptProposedAction();
    }
    void dropEvent(QDropEvent *event) override {
        QList<QUrl> urls = event->mimeData()->urls();
        if(urls.isEmpty()) return;
        QString path = urls.first().toLocalFile();
        if(!path.isEmpty()){ selectedFile=path; fileEdit->setText(path); }
    }

private:
    QLineEdit* fileEdit;
    QLabel* hashLabel;
    QComboBox* algoCombo;
    QString selectedFile;
    QString currentFunnyHash;
};

int main(int argc,char* argv[]){
    QApplication a(argc,argv);
    FunnyHashWidget w;
    w.resize(500,400);
    w.setWindowTitle("Multi-Hash Funny Hash Generator & Embedder");
    w.show();
    return a.exec();
}

#include "main.moc"
